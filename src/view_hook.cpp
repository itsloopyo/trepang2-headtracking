// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Inject the head pose into the render path, and nowhere else.
//
// GetPlayerViewPoint fires from several call sites per frame. Only the one the
// caller gate names - ULocalPlayer::GetViewPoint, which builds the scene view
// and every world-to-screen projection the HUD asks for - gets the pose written
// back. Every other caller keeps the clean rotation. The player's shots do not
// read the view at all: BaseWeapon::GetShootLocation / GetShootAngles resolve to
// BasePlayer's GetActorEyesViewPoint override, which returns
// FirstPersonCameraComponent's world transform, and the mod never writes to that
// component.

#include "view_hook.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <tuple>
#include <unordered_map>

#include <windows.h>
#include <psapi.h>
#include <intrin.h>

#include "ads.h"
#include "ads_gate.h"
#include "ads_pose.h"
#include "aim_projection.h"
#include "aim_trace.h"
#include "builds/build_registry.h"
#include "camera_boundary.h"
#include "camera_fov.h"
#include "dev_console.h"
#include "game_state.h"
#include "inject_mode.h"
#include "lean_trace.h"
#include "logging.h"
#include "marker_rule.h"
#include "player_rig.h"
#include "reticle.h"
#include "torch_aim.h"
#include "tracking.h"
#include "udp_link.h"
#include "ue4_types.h"
#include "ue_call.h"
#include "ue_vm.h"
#include "window_centering.h"

#include "cameraunlock/camera/lean_clamp.h"
#include "cameraunlock/camera/zoom_compensation.h"
#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/unreal/ue_math.h"
#include "cameraunlock/unreal/ue_runtime.h"

namespace t2_ht::view_hook {

namespace {

namespace ue = ::cameraunlock::unreal;

using ue::FQuat4d;
using ue::FRotator;
using ue::FVector;
using cameraunlock::time::FrameClock;

static_assert(std::tuple_size<decltype(OffsetTable::kKnownCallerRvas)>::value
                  == inject::kCallerSlots,
              "inject::kCallerSlots must match the profile's caller RVA table");

using GetPlayerViewPoint_t =
    void(__fastcall*)(void* self, ue4::FVector* outLocation, ue4::FRotator* outRotation);

Dependencies g_deps{};

std::atomic<bool> g_trackingEnabled{true};
std::atomic<bool> g_worldSpaceYaw{true};
std::atomic<int>  g_injectMode{inject::kFirstCaller};

GetPlayerViewPoint_t g_origGetPlayerViewPoint = nullptr;
void* g_hookTarget = nullptr;
std::atomic<std::uint64_t> g_hookCallCount{0};

FrameClock g_frameClock;
cameraunlock::camera::LeanClamp g_leanClamp;

// The render caller runs more than once per engine frame (the scene view, and
// every projection the HUD asks the local player for). All of them must see the
// same pose, so the first call of a frame does the work and the rest replay it.
// A repeat is recognised by the engine's own frame number and the identical
// clean view. A millisecond clock cannot tell two frames apart at high frame
// rates, and a false repeat would hold the previous frame's head pose.
struct FrameCache {
    bool Valid = false;
    std::int64_t Frame = 0;
    ue4::FVector CleanLocation{0.0f, 0.0f, 0.0f};
    ue4::FRotator CleanRotation{0.0f, 0.0f, 0.0f};
    ue4::FVector OutLocation{0.0f, 0.0f, 0.0f};
    ue4::FRotator OutRotation{0.0f, 0.0f, 0.0f};
};
FrameCache g_frameCache;
AimSample g_lastAim;
bool g_markOverride = false;
FVector g_markOverridePoint{0.0, 0.0, 0.0};

ue_vm::ResolveRetry g_frameCountRetry;
ue_call::Function g_getFrameCount;   // KismetSystemLibrary::GetFrameCount
std::uintptr_t g_kismetSystemCdo = 0;

// GFrameCounter, through UKismetSystemLibrary::GetFrameCount. False until the
// function resolves, and then the cache simply never replays.
bool EngineFrame(std::int64_t& out) {
    if (!g_getFrameCount.Ready()) {
        if (!g_frameCountRetry.Due() || !ue_vm::Ready()) return false;
        g_kismetSystemCdo = ue_call::DefaultObject("KismetSystemLibrary");
        if (!g_kismetSystemCdo ||
            !g_getFrameCount.Resolve("KismetSystemLibrary", "GetFrameCount",
                                     {{"ReturnValue", sizeof(std::int64_t)}}))
            return false;
    }
    ue_call::Frame frame(g_getFrameCount);
    if (!frame.Call(g_kismetSystemCdo)) return false;
    out = frame.Get<std::int64_t>(0);
    return true;
}

bool SameView(const ue4::FVector& a, const ue4::FVector& b) { return a.X == b.X && a.Y == b.Y && a.Z == b.Z; }
bool SameView(const ue4::FRotator& a, const ue4::FRotator& b) {
    return a.Pitch == b.Pitch && a.Yaw == b.Yaw && a.Roll == b.Roll;
}

// How long the view takes to ease onto the head pose when tracking starts
// applying again.
constexpr std::uint64_t kEntryEaseMs = 400;

// The tick the pose started applying, 0 while it is not applied.
std::uint64_t g_poseSinceMs = 0;

float EntryEase(std::uint64_t elapsedMs) {
    if (elapsedMs >= kEntryEaseMs) return 1.0f;
    const float t = static_cast<float>(elapsedMs) / static_cast<float>(kEntryEaseMs);
    return t * t * (3.0f - 2.0f * t);
}

constexpr std::uint64_t kHeartbeatMs = 30000;
constexpr std::uint64_t kZoomSettleMs = 1000;
constexpr std::uint64_t kCallerSummaryEvery = 3000;
constexpr double kMaxTraceCm = 100000.0;

// A zoom factor moving by less than this is the same factor: it is a quarter of
// the last digit the log prints, so a change below it could not be read anyway.
constexpr float kFactorEpsilon = 0.0005f;

// ---- caller discovery ----------------------------------------------------
struct CallerStats { std::uint64_t Count = 0; std::int64_t Stride = 0; };
std::mutex g_callerMutex;
std::unordered_map<std::uintptr_t, CallerStats> g_callerCounts;
std::atomic<std::uint64_t> g_callerLastSummary{0};

void CountCaller(std::uintptr_t retRva, std::uint64_t call, std::int64_t stride) {
    {
        std::lock_guard<std::mutex> lk(g_callerMutex);
        CallerStats& c = g_callerCounts[retRva];
        ++c.Count;
        c.Stride = stride;
    }
    if (call - g_callerLastSummary.load(std::memory_order_relaxed) < kCallerSummaryEvery) return;
    g_callerLastSummary.store(call, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lk(g_callerMutex);
    Log::Line("caller-summary @%llu calls: %zu unique return RVAs:",
              static_cast<unsigned long long>(call), g_callerCounts.size());
    for (const auto& kv : g_callerCounts)
        Log::Line("  ret RVA 0x%08llx  count=%llu  rot-loc=%lld",
                  static_cast<unsigned long long>(kv.first),
                  static_cast<unsigned long long>(kv.second.Count),
                  static_cast<long long>(kv.second.Stride));
}

std::uintptr_t ReturnRva(const void* returnAddress) {
    const auto addr = reinterpret_cast<std::uintptr_t>(returnAddress);
    return ue::ModuleBase() != 0 ? addr - ue::ModuleBase() : addr;
}

float Clamp1(float ndc) { return ndc < -1.0f ? -1.0f : (ndc > 1.0f ? 1.0f : ndc); }

// ---- per-frame state shared with the log ---------------------------------
struct FrameReport {
    TrackingState State;
    game_state::Verdict Gate;
    bool HavePose = false;
    AdsEntryPose::Pose Applied;
    float ZoomFactor = 1.0f;
    float RenderFov = 0.0f;
    float BaseFov = 0.0f;
    float TanX = 0.0f, TanY = 0.0f;
    FVector PositionOffset{0.0, 0.0, 0.0};
    bool TraceValid = false;
    bool TraceHit = false;
    double TraceDistance = 0.0;
    FVector AimPoint{0.0, 0.0, 0.0};
    aim_projection::Ndc Mark;
    // Whether the mark landed inside the frame. A valid mark outside it is
    // still published, pinned to the edge, so this is not "was the crosshair
    // moved" - it is the column that tells a pinned crosshair from a free one.
    bool MarkOnScreen = false;
    bool MarkerShown = false;
};

std::atomic<bool> g_reportRequested{false};

void LogHeartbeat(const FrameReport& r, std::uintptr_t retRva) {
    static std::uint64_t s_last = 0;
    const std::uint64_t now = GetTickCount64();
    const bool requested = g_reportRequested.exchange(false);
    if (!requested && s_last != 0 && now - s_last < kHeartbeatMs) return;
    s_last = now;

    // Through udp_link's classifier rather than a second walk of the same three
    // flags: the hand-rolled one here could never report "receiving", so a
    // heartbeat taken with a tracker feeding the mod said "listening".
    auto* receiver = tracking::Receiver();
    const char* udpPort =
        !receiver ? "none"
                  : udp_link::LinkStateName(udp_link::ClassifyLink(
                        receiver->IsRetrying(), receiver->IsRunning(), receiver->IsReceiving()));
    Log::Line("heartbeat ret=0x%08llx tracking=%s verdict=%s gate=%s viewOff=%.1fcm/%.1fdeg "
              "aiming=%d adsMode=%s udp=%s pose=%d "
              "applied=(Y%.2f P%.2f R%.2f x%.3f y%.3f z%.3f) fov=%.2f base=%.2f zoom=%.4f "
              "constraint=%d refAspect=%.3f tan=(%.4f,%.4f) lean=%s torch=%s",
              static_cast<unsigned long long>(retRva),
              g_trackingEnabled.load() ? "ON" : "OFF", Reason(r.State.verdict),
              game_state::BlockerName(r.Gate.Why), r.Gate.ViewOffsetCm, r.Gate.ViewAngleDeg,
              r.State.aiming ? 1 : 0, AdsModeValue(GetAdsMode()),
              udpPort, r.HavePose ? 1 : 0, r.Applied.yaw, r.Applied.pitch, r.Applied.roll,
              r.Applied.x, r.Applied.y, r.Applied.z, r.RenderFov, r.BaseFov, r.ZoomFactor,
              camera_fov::AspectConstraint(), camera_fov::ReferenceAspect(), r.TanX, r.TanY,
              !g_deps.config->collision_enabled ? "off"
                  : lean_trace::Failed()        ? "unavailable"
                  : g_leanClamp.InContact()     ? "contact"
                                                : "clear",
              torch_aim::StateName());
    Log::Line("aim trace=%d hit=%d dist=%.1fcm point=(%.1f,%.1f,%.1f) "
              "posOff=(%.2f,%.2f,%.2f) mark=%d ndc=(%.4f,%.4f) onScreen=%d adsMarker=%d",
              r.TraceValid ? 1 : 0, r.TraceHit ? 1 : 0, r.TraceDistance,
              r.AimPoint.X, r.AimPoint.Y, r.AimPoint.Z, r.PositionOffset.X, r.PositionOffset.Y,
              r.PositionOffset.Z, r.Mark.Valid ? 1 : 0, r.Mark.X, r.Mark.Y, r.MarkOnScreen ? 1 : 0,
              r.MarkerShown ? 1 : 0);
}

void LogLeanClamp(const cameraunlock::math::Vec3& wanted,
                  const cameraunlock::math::Vec3& allowed) {
    static bool s_contact = false;
    static bool s_failed = false;
    const bool failed = g_leanClamp.LastQueryFailed();
    if (failed != s_failed) {
        s_failed = failed;
        Log::Line("lean-clamp: sweep %s", failed ? "FAILED - the lean is running unclamped"
                                                  : "working again");
    }
    const bool contact = g_leanClamp.InContact();
    if (contact != s_contact) {
        s_contact = contact;
        Log::Line("lean-clamp: %s (wanted %.1fcm, allowed %.1fcm)%s%s",
                  contact ? "holding the view off geometry" : "clear",
                  wanted.Magnitude(), allowed.Magnitude(), contact ? " on " : "",
                  contact ? lean_trace::LastHitDescription().c_str() : "");
    }
}

// The render window, kept between frames. Finding it enumerates every top-level
// window on the desktop and holds the window-list lock while it does, which is
// not something to repeat once a frame in a hook the HUD drives; the handle only
// stops being valid when the engine tears the window down, and IsWindow says so
// without enumerating anything.
HWND RenderWindow() {
    static HWND s_window = nullptr;
    if (!s_window || !IsWindow(s_window)) s_window = window_centering::FindRenderWindow();
    return s_window;
}

// Half-field tangents of the frame as the engine will draw it.
bool FrameTangents(float fov, float& tanX, float& tanY) {
    RECT rc{};
    const HWND wnd = RenderWindow();
    if (!wnd || !GetClientRect(wnd, &rc) || rc.right <= 0 || rc.bottom <= 0) return false;
    const float aspect = static_cast<float>(rc.right) / static_cast<float>(rc.bottom);
    return camera_fov::HalfFieldTangents(camera_fov::AspectConstraint(), fov, aspect,
                                         camera_fov::ReferenceAspect(), tanX, tanY);
}

AimSample Sample(const FVector& origin, const FVector& dir, const FrameReport& r,
                 const ue4::FVector& eye) {
    return AimSample{true, origin.X, origin.Y, origin.Z, dir.X, dir.Y, dir.Z, r.TraceHit,
                     r.AimPoint.X, r.AimPoint.Y, r.AimPoint.Z, eye.X, eye.Y, eye.Z,
                     r.Mark.X, r.Mark.Y, r.Mark.Valid};
}

// 1.0 - the pose passes through unscaled - whenever either field of view is
// unreadable, rather than a guess at what the game is zooming by.
float ZoomFactor(float renderFov, float baseFov) {
    if (!camera_fov::Plausible(renderFov) || !camera_fov::Plausible(baseFov)) return 1.0f;
    return cameraunlock::camera::FovZoomFactor(camera_fov::TanHalf(renderFov),
                                               camera_fov::TanHalf(baseFov));
}

// Both FOVs are the camera's scalar in degrees - the live one and the video
// setting it is built from - so the factor reads 1.0000 at the hip in gameplay,
// with or without a tracker; anything else there means the units do not match.
// Logged once the factor has held still for a second, and again if it settles
// somewhere new (a changed FOV setting).
void LogZoomTerms(const FrameReport& r, bool aimingDownSights, std::uint64_t tick) {
    static float s_loggedFactor = 0.0f;
    static float s_heldFactor = 0.0f;
    static std::uint64_t s_heldSince = 0;
    if (!r.Gate.InGameplay || aimingDownSights || !camera_fov::Plausible(r.RenderFov) ||
        !camera_fov::Plausible(r.BaseFov))
        return;
    if (std::fabs(r.ZoomFactor - s_heldFactor) > kFactorEpsilon) {
        s_heldFactor = r.ZoomFactor;
        s_heldSince = tick;
        return;
    }
    if (tick - s_heldSince < kZoomSettleMs ||
        std::fabs(r.ZoomFactor - s_loggedFactor) <= kFactorEpsilon)
        return;
    s_loggedFactor = r.ZoomFactor;
    Log::Line("fov: zoom compensation terms at the hip: render %.2f deg, base (FOV "
              "setting) %.2f deg, constraint %d, reference aspect %.3f, factor %.4f",
              r.RenderFov, r.BaseFov, camera_fov::AspectConstraint(),
              camera_fov::ReferenceAspect(), r.ZoomFactor);
}

// A field of view that does not read leaves the pose unscaled rather than
// scaled by a guess, and the log has to say so: a factor parked at 1.0 through a
// scope looks exactly like a game that never zooms. The 0.00 in the line says
// which of the two went missing.
void LogFovReadable(const FrameReport& r) {
    static bool s_readable = true;
    if (!r.Gate.InGameplay) return;
    const bool readable = camera_fov::Plausible(r.RenderFov) && camera_fov::Plausible(r.BaseFov);
    if (readable == s_readable) return;
    s_readable = readable;
    Log::Line("fov: render %.2f deg, game FOV setting %.2f deg - %s", r.RenderFov, r.BaseFov,
              readable ? "both readable again, zoom compensation is back on"
                       : "not both readable, so the pose is applied with no zoom "
                         "compensation");
}

// On a change in what the trace is DOING, not in what it stops on: the object
// under the crosshair changes several times a second in ordinary play, and a
// line each time is per-frame logging wearing a state change's clothes. The live
// point and distance ride the heartbeat.
void LogAimTrace(const aim_trace::Result& hit) {
    static int s_state = -1;
    const int state = !hit.Valid ? 0 : hit.Hit ? 1 : 2;
    if (state == s_state) return;
    s_state = state;
    if (state == 0)
        Log::Line("aim-trace: not running - no mark is drawn, and the game's own "
                  "crosshair stays where it laid it out");
    else if (state == 2)
        Log::Line("aim-trace: running, nothing within %.0fcm of the aim", kMaxTraceCm);
    else
        Log::Line("aim-trace: running, stops on %s %s / %s %s at %.1fcm",
                  hit.Actor ? ue::ClassName(hit.Actor).c_str() : "-",
                  hit.Actor ? ue::ObjectName(hit.Actor).c_str() : "-",
                  hit.Component ? ue::ClassName(hit.Component).c_str() : "-",
                  hit.Component ? ue::ObjectName(hit.Component).c_str() : "-", hit.Distance);
}

// The pose as it reaches the camera. `entry` eases the whole of it in coming out
// of a menu, a cutscene, a load or a tracker dropout, where the frame before was
// the game's clean camera and the head can be anywhere by now. The zoom factor
// then scales back what a narrowed field of view magnifies - all but roll, which
// rotates the image rather than moving it across the frame and so is the same
// tilt at every field of view.
void ShapePose(AdsEntryPose::Pose& pose, float entry, float zoomFactor) {
    pose.yaw *= entry;
    pose.pitch *= entry;
    pose.roll *= entry;
    pose.x *= entry;
    pose.y *= entry;
    pose.z *= entry;

    pose.yaw = cameraunlock::camera::ScaleAngleForZoom(pose.yaw, zoomFactor);
    pose.pitch = cameraunlock::camera::ScaleAngleForZoom(pose.pitch, zoomFactor);
    pose.x *= zoomFactor;
    pose.y *= zoomFactor;
    pose.z *= zoomFactor;
}

// As much of the wanted lean as the level leaves room for, swept from the CLEAN
// eye. Passed through untouched when the clamp is switched off in the INI.
FVector ClampLean(const FVector& cleanLocation, const FVector& wanted, float dt,
                  std::uintptr_t pawn) {
    if (!g_deps.config->collision_enabled) return wanted;
    lean_trace::SetPawn(pawn);
    const cameraunlock::math::Vec3 from{static_cast<float>(cleanLocation.X),
                                        static_cast<float>(cleanLocation.Y),
                                        static_cast<float>(cleanLocation.Z)};
    const cameraunlock::math::Vec3 want{static_cast<float>(wanted.X),
                                        static_cast<float>(wanted.Y),
                                        static_cast<float>(wanted.Z)};
    const cameraunlock::math::Vec3 allowed =
        g_leanClamp.Apply(from, want, dt, &lean_trace::Query, nullptr);
    LogLeanClamp(want, allowed);
    return FVector{allowed.x, allowed.y, allowed.z};
}

// The mark: the point the shot's ray stops on, projected from the eye the frame
// is drawn from, through the rotation the frame is drawn with - both read back
// from the floats actually written, so the projection describes the camera the
// player is looking through.
aim_projection::Ndc ProjectMark(const ue4::FVector& eye, const ue4::FRotator& drawn,
                                const aim_trace::Result& hit, const FVector& aimDir,
                                float tanX, float tanY) {
    const aim_projection::View view{
        ue4::ToCore(eye), ue::QuatFromEulerDeg(drawn.Pitch, drawn.Yaw, drawn.Roll), tanX, tanY};
    if (g_markOverride) return aim_projection::ProjectPoint(view, g_markOverridePoint);
    return hit.Hit ? aim_projection::ProjectPoint(view, hit.Point)
                   : aim_projection::ProjectDirection(view, aimDir);
}

// True when this call is the render caller repeating inside one engine frame, in
// which case it is handed that frame's view back rather than a second pose.
bool ReplayFrame(bool haveFrame, std::int64_t engineFrame, const ue4::FVector& cleanLocation,
                 const ue4::FRotator& cleanRotation, ue4::FVector* outLocation,
                 ue4::FRotator* outRotation) {
    if (!haveFrame || !g_frameCache.Valid || engineFrame != g_frameCache.Frame ||
        !SameView(cleanLocation, g_frameCache.CleanLocation) ||
        !SameView(cleanRotation, g_frameCache.CleanRotation))
        return false;
    *outLocation = g_frameCache.OutLocation;
    *outRotation = g_frameCache.OutRotation;
    static bool s_logged = false;
    if (!s_logged) {
        s_logged = true;
        Log::Line("hook: render caller repeats within engine frame %lld replay that frame's view",
                  static_cast<long long>(engineFrame));
    }
    return true;
}

// One render frame, from the clean view the engine handed back to the view the
// player sees. Everything it changes goes through outLocation / outRotation.
void ApplyFrame(std::uintptr_t controller, std::uintptr_t retRva, ue4::FVector* outLocation,
                ue4::FRotator* outRotation, const ue4::FVector& cleanLocationRaw,
                const ue4::FRotator& cleanRaw, std::uint64_t tick) {
    FrameReport report;
    const FRotator clean = ue4::ToCore(cleanRaw);
    const FVector cleanLocation = ue4::ToCore(cleanLocationRaw);
    const FQuat4d cleanQ = ue::QuatFromEulerDeg(clean.Pitch, clean.Yaw, clean.Roll);

    camera_fov::RefreshAspectConstraint(controller);
    report.RenderFov = camera_fov::ReadRenderFov(outLocation, outRotation);
    report.BaseFov = camera_fov::BaseFov();
    report.ZoomFactor = ZoomFactor(report.RenderFov, report.BaseFov);

    const player_rig::Snapshot rig = player_rig::Read(controller);
    report.Gate = game_state::Evaluate(controller, rig, cleanLocation, clean);
    game_state::LogTransitions(report.Gate);

    Session* session = tracking::Get();
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    const float dt = g_frameClock.Tick();
    if (session && session->Update(dt))
        report.HavePose = session->GetRotation(yaw, pitch, roll);

    LogZoomTerms(report, rig.AimingDownSights, tick);
    LogFovReadable(report);

    report.State = DecideTracking(report.Gate, g_trackingEnabled.load(std::memory_order_relaxed),
                                  report.HavePose, rig.AimingDownSights, GetAdsMode());

    // The shot's own ray, cast into the world this frame. BasePlayer overrides
    // GetActorEyesViewPoint to return FirstPersonCameraComponent's world
    // transform, and GetShootLocation / GetShootAngles hand exactly that to the
    // weapon, so the ray starts there and runs along that component's forward -
    // not along the drawn view, which carries the camera manager's shakes on
    // top. Done in every gameplay frame, not only tracked ones, so the shot log
    // compares like with like.
    const FVector aimOrigin = rig.FirstPersonCamera.Valid ? rig.FirstPersonCamera.Position : cleanLocation;
    const FVector aimDir = rig.FirstPersonCamera.Valid
        ? rig.FirstPersonCamera.Forward
        : ue::QuatRotateVec(cleanQ, FVector{1.0, 0.0, 0.0});
    aim_trace::Result hit;
    if (report.Gate.InGameplay) {
        hit = aim_trace::Cast(rig.Pawn, rig.Weapon, aimOrigin, aimDir, kMaxTraceCm);
        report.TraceValid = hit.Valid;
        report.TraceHit = hit.Hit;
        report.TraceDistance = hit.Distance;
        report.AimPoint = hit.Point;
        LogAimTrace(hit);
    }

    if (!PoseApplies(report.State.verdict)) {
        g_poseSinceMs = 0;
        ads_pose::Reset();
        g_leanClamp.Reset();
        torch_aim::Center(rig);
        reticle::Publish(controller, rig.Pawn, false, 0.0f, 0.0f);
        reticle::PublishMarker(controller, false, 0.0f, 0.0f);
        g_lastAim = Sample(aimOrigin, aimDir, report, *outLocation);
        LogHeartbeat(report, retRva);
        return;
    }

    float offX = 0.0f, offY = 0.0f, offZ = 0.0f;
    const bool havePosition = session->GetPositionOffset(offX, offY, offZ);

    AdsEntryPose::Pose absolute;
    absolute.yaw = yaw;
    absolute.pitch = pitch;
    absolute.roll = roll;
    absolute.x = offX;
    absolute.y = offY;
    absolute.z = offZ;
    AdsEntryPose::Pose pose = ads_pose::Advance(report.State, report.HavePose, absolute, tick).Pose;

    if (g_poseSinceMs == 0) g_poseSinceMs = tick;
    ShapePose(pose, EntryEase(tick - g_poseSinceMs), report.ZoomFactor);
    report.Applied = pose;

    FRotator rotation = clean;
    const bool worldSpaceYaw = g_worldSpaceYaw.load(std::memory_order_relaxed);
    camera_boundary::ApplyHeadPose(rotation, pose.yaw, pose.pitch, pose.roll, worldSpaceYaw);
    // The torch takes the same pose through the same composition, multiplied,
    // so the beam leads the view rather than parting company with it.
    torch_aim::Apply(rig, pose.yaw, pose.pitch, pose.roll, worldSpaceYaw);
    FVector eye = cleanLocation;
    if (havePosition) {
        const FVector offset =
            ClampLean(cleanLocation, camera_boundary::PositionOffset(cleanQ, pose.x, pose.y, pose.z),
                      dt, rig.Pawn);
        eye.X += offset.X;
        eye.Y += offset.Y;
        eye.Z += offset.Z;
        report.PositionOffset = offset;
    } else {
        // Rotation-only mode applies a pose but no lean, so the clamp never runs
        // and never releases. Without this the allowance stays frozen at the last
        // 6DOF frame's, and cycling back to full tracking rations the first lean
        // against a wall the player walked away from several rooms ago.
        g_leanClamp.Reset();
    }
    *outRotation = ue4::FromCore(rotation);
    *outLocation = ue4::FromCore(eye);

    if (hit.Valid && FrameTangents(report.RenderFov, report.TanX, report.TanY))
        report.Mark = ProjectMark(*outLocation, *outRotation, hit, aimDir, report.TanX, report.TanY);

    g_lastAim = Sample(aimOrigin, aimDir, report, *outLocation);

    // The game's crosshair follows the impact point whenever the pose is applied.
    // Raising the sights hides it, so in the `marker` ADS mode the mod shows its
    // own instance of the crosshair there instead.
    // Clamped to the frame edge rather than dropped. The mark leaves the frame
    // at about 25 degrees of head pitch on a 16:9 display at the game's default
    // field of view, which is ordinary head movement, and handing the reticle
    // `false` there restores the game's own layout - a crosshair drawn dead
    // centre, the one place the rounds are certainly not going. A mark with no
    // direction at all still restores: with no projection there is nothing
    // better to say than what the game already drew.
    const bool onScreen = aim_projection::OnScreen(report.Mark);
    report.MarkOnScreen = onScreen;
    reticle::Publish(controller, rig.Pawn, report.Mark.Valid, Clamp1(report.Mark.X),
                     Clamp1(report.Mark.Y));
    report.MarkerShown = reticle::PublishMarker(
        controller, marker_rule::ShowAdsMarker(true, report.State.aiming, GetAdsMode(), onScreen),
        report.Mark.X, report.Mark.Y);

    LogHeartbeat(report, retRva);
}

void __fastcall GetPlayerViewPoint_Hook(void* self, ue4::FVector* outLocation, ue4::FRotator* outRotation) {
    const std::uintptr_t retRva = ReturnRva(_ReturnAddress());
    const auto controller = reinterpret_cast<std::uintptr_t>(self);

    g_origGetPlayerViewPoint(self, outLocation, outRotation);

    const auto call = g_hookCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const int mode = g_injectMode.load(std::memory_order_relaxed);

    dev_console::Poll(controller);

    if (mode == inject::kAllCallers) {
        CountCaller(retRva, call,
                    static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(outRotation)) -
                    static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(outLocation)));
        return;
    }
    if (!inject::ShouldInject(retRva, mode, Offsets().kKnownCallerRvas)) return;

    const ue4::FRotator cleanRaw = *outRotation;
    const ue4::FVector cleanLocationRaw = *outLocation;
    std::int64_t engineFrame = 0;
    const bool haveFrame = EngineFrame(engineFrame);
    if (ReplayFrame(haveFrame, engineFrame, cleanLocationRaw, cleanRaw, outLocation, outRotation))
        return;

    ApplyFrame(controller, retRva, outLocation, outRotation, cleanLocationRaw, cleanRaw,
               GetTickCount64());

    g_frameCache = FrameCache{haveFrame, engineFrame,  cleanLocationRaw,
                              cleanRaw,  *outLocation, *outRotation};
}

}  // namespace

bool Install(const Dependencies& deps) {
    g_deps = deps;
    g_worldSpaceYaw.store(deps.config->world_space_yaw);
    g_injectMode.store(Offsets().kDefaultInjectMode);

    const HMODULE host = GetModuleHandleW(nullptr);
    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), host, &mi, sizeof(mi))) {
        Log::Line("FATAL: GetModuleInformation on the host module failed (%lu) - no "
                  "hook installed", GetLastError());
        return false;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(host);
    ue::SetRuntime(base, base + mi.SizeOfImage, Offsets().UObjectGlobals);

    aim_trace::SetTraceChannel(deps.config->aim_trace_channel);
    lean_trace::SetMargin(deps.config->collision_margin);
    lean_trace::SetChannel(deps.config->collision_channel);
    torch_aim::Configure(deps.config->light_follows_head, deps.config->light_multiplier);
    cameraunlock::camera::LeanClampSettings clamp;
    clamp.skin = 0.0f;  // lean_trace carries the margin along the surface normal
    clamp.release_smoothing = deps.config->collision_release_smoothing;
    g_leanClamp.SetSettings(clamp);

    auto& hm = cameraunlock::hooks::HookManager::Instance();
    if (auto s = hm.Initialize(); s != cameraunlock::hooks::HookStatus::Ok &&
                                  s != cameraunlock::hooks::HookStatus::ErrorAlreadyInitialized) {
        Log::Line("FATAL: MinHook Initialize failed: %s",
                  cameraunlock::hooks::HookStatusToString(s));
        return false;
    }

    g_hookTarget = reinterpret_cast<void*>(base + Offsets().kGetPlayerViewPointRva);
    if (auto s = hm.CreateHook(g_hookTarget, reinterpret_cast<void*>(&GetPlayerViewPoint_Hook),
                               reinterpret_cast<void**>(&g_origGetPlayerViewPoint));
        s != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("FATAL: CreateHook(GetPlayerViewPoint) failed: %s",
                  cameraunlock::hooks::HookStatusToString(s));
        g_hookTarget = nullptr;
        return false;
    }
    if (auto s = hm.EnableHook(g_hookTarget); s != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("FATAL: EnableHook(GetPlayerViewPoint) failed: %s",
                  cameraunlock::hooks::HookStatusToString(s));
        g_hookTarget = nullptr;
        return false;
    }

    Log::Line("hook: GetPlayerViewPoint at RVA 0x%08llx, module base 0x%llx, inject mode %d, "
              "ProcessEvent %s",
              static_cast<unsigned long long>(Offsets().kGetPlayerViewPointRva),
              static_cast<unsigned long long>(base), g_injectMode.load(),
              ue_vm::Ready() ? "resolved" : "UNAVAILABLE");
    if (g_injectMode.load() == inject::kAllCallers)
        Log::Line("hook: caller-discovery mode - nothing is written to the camera");
    return true;
}

void Shutdown() {
    if (!g_hookTarget) return;
    cameraunlock::hooks::HookManager::Instance().DisableHook(g_hookTarget);
    g_hookTarget = nullptr;
}

bool TrackingEnabled() { return g_trackingEnabled.load(); }
void SetTrackingEnabled(bool enabled) { g_trackingEnabled.store(enabled); }

bool WorldSpaceYaw() { return g_worldSpaceYaw.load(); }
void SetWorldSpaceYaw(bool worldSpaceYaw) { g_worldSpaceYaw.store(worldSpaceYaw); }

AimSample LastAim() { return g_lastAim; }

void RequestReport() { g_reportRequested.store(true); }

void SetMarkOverride(bool valid, double x, double y, double z) {
    g_markOverridePoint = FVector{x, y, z};
    g_markOverride = valid;
}

int  InjectMode() { return g_injectMode.load(); }
void SetInjectMode(int mode) { g_injectMode.store(mode); }

}  // namespace t2_ht::view_hook
