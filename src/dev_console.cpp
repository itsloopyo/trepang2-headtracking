// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "dev_console.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <sstream>
#include <vector>

#include <windows.h>

#include "ads.h"
#include "inject_mode.h"
#include "ue4_types.h"
#include "builds/build_registry.h"
#include "logging.h"
#include "ue_call.h"
#include "ue_reflect.h"
#include "ue_vm.h"
#include "view_hook.h"

#include "process_event_hook.h"

#include "cameraunlock/memory/safe_memory.h"
#include "cameraunlock/unreal/ue_math.h"
#include "cameraunlock/unreal/ue_runtime.h"

namespace t2_ht::dev_console {

namespace {

namespace ue = ::cameraunlock::unreal;

bool g_enabled = false;

std::mutex g_peMutex;
std::unordered_map<std::uintptr_t, int> g_peSeen;
std::uintptr_t g_peTarget = 0;
std::uint64_t g_peUntil = 0;

void PeObserver(std::uintptr_t self, std::uintptr_t fn, void*) {
    if (g_peTarget && self != g_peTarget) return;
    std::lock_guard<std::mutex> lk(g_peMutex);
    ++g_peSeen[fn];
}

// Shot log: where each projectile starts and where it lands, next to where the
// mod put the crosshair on that frame.
std::uintptr_t g_fnOnImpact = 0;
std::uintptr_t g_fnSpawnedBullet = 0;
std::size_t g_impactPointOffset = 0;
std::vector<std::uintptr_t> g_pendingProjectiles;

void OnShotEvent(std::uintptr_t self, std::uintptr_t fn, void* params) {
    if (!params) return;
    const view_hook::AimSample a = view_hook::LastAim();
    if (fn == g_fnSpawnedBullet) {
        std::uintptr_t projectile = 0;
        std::memcpy(&projectile, params, sizeof(projectile));
        if (projectile) g_pendingProjectiles.push_back(projectile);
        return;
    }
    ue4::FVector impact{};
    std::memcpy(&impact, static_cast<const char*>(params) + g_impactPointOffset, sizeof(impact));
    Log::Line("shot: impact %s at (%.2f,%.2f,%.2f) | mark point (%.2f,%.2f,%.2f) hit=%d delta=(%.2f,%.2f,%.2f) "
              "ndc=(%.4f,%.4f) valid=%d eye=(%.2f,%.2f,%.2f)",
              ue::ClassName(self).c_str(), impact.X, impact.Y, impact.Z, a.PointX, a.PointY, a.PointZ,
              a.Hit ? 1 : 0, impact.X - a.PointX, impact.Y - a.PointY, impact.Z - a.PointZ, a.NdcX, a.NdcY,
              a.NdcValid ? 1 : 0, a.EyeX, a.EyeY, a.EyeZ);
}

bool ActorLocation(std::uintptr_t actor, ue4::FVector& out);

void InstallShotLog() {
    static bool s_tried = false;
    if (s_tried) return;
    s_tried = true;
    g_fnOnImpact = ue::FindLiveObject("Function", "OnImpact", "BaseProjectile");
    g_fnSpawnedBullet = ue::FindLiveObject("Function", "BPOnSpawnedBullet", "BaseWeaponBP_C");
    const std::uintptr_t hitResult = ue::FindLiveObject("ScriptStruct", "HitResult", nullptr);
    std::vector<ue_reflect::FieldInfo> h, p;
    if (!g_fnOnImpact || !g_fnSpawnedBullet || !hitResult ||
        !ue_reflect::ResolveAll("HitResult", hitResult, {"ImpactPoint"}, h) ||
        !ue_reflect::ResolveAll("OnImpact", g_fnOnImpact, {"HitResult"}, p)) {
        Log::Line("dev: shot log unavailable");
        return;
    }
    g_impactPointOffset = p[0].Offset + h[0].Offset;
    if (!process_event_hook::Install() ||
        !process_event_hook::AddPostHandler(g_fnOnImpact, &OnShotEvent) ||
        !process_event_hook::AddPostHandler(g_fnSpawnedBullet, &OnShotEvent)) {
        Log::Line("dev: shot log hook failed");
        return;
    }
    Log::Line("dev: shot log armed (BaseProjectile::OnImpact, BaseWeaponBP_C::BPOnSpawnedBullet)");
}

std::string g_path;
std::uint64_t g_lastPollMs = 0;

struct alignas(8) FStringHeader {
    const wchar_t* Data;
    std::int32_t   Num;
    std::int32_t   Max;
};

// ProcessEvent fills a UFunction's WHOLE parameter frame - every parameter and
// every local - so a frame bigger than the buffer handed to it writes off the
// end of that buffer. Every dispatch through a fixed-size stack buffer below
// asks this first.
bool FrameFits(const char* what, std::uintptr_t fn, std::size_t bytes) {
    const std::size_t size = ue_reflect::StructSize(fn);
    if (size != 0 && size <= bytes) return true;
    Log::Line("dev: %s has a %zu-byte parameter frame, over the %zu this command "
              "allocates", what, size, bytes);
    return false;
}

// strtol / strtod, never std::stoi / std::stof: the text comes out of
// HeadTracking.devcmd, and the throwing forms turn a typo into an uncaught C++
// exception on the game thread inside the render detour, which takes the game
// with it.
// Trailing blanks are fine - a hand-typed command line collects them - but
// nothing else may follow the number.
bool NothingButBlanksLeft(const char* start, const char* end) {
    if (end == start) return false;
    while (*end == ' ' || *end == '\t' || *end == '\r') ++end;
    return *end == 0;
}

bool ParseInt(const std::string& text, long& out) {
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (!NothingButBlanksLeft(text.c_str(), end)) return false;
    out = value;
    return true;
}

bool ParseDouble(const std::string& text, double& out) {
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (!NothingButBlanksLeft(text.c_str(), end)) return false;
    out = value;
    return true;
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

void DumpClassChain(const char* label, std::uintptr_t cls) {
    for (int depth = 0; cls && depth < 12; ++depth) {
        ue_reflect::DumpProperties((std::string(label) + " : " + ue::ObjectName(cls)).c_str(), cls);
        std::uintptr_t super = 0;
        if (!ue::SafeReadPtr(cls + Offsets().Reflection.kUStruct_SuperStruct, super)) break;
        cls = super;
    }
}

void FindObjects(const std::string& needle, const std::string& classFilter) {
    int shown = 0;
    ue::ForEachUObject([&](std::uintptr_t obj) {
        const std::string name = ue::ObjectName(obj);
        if (!ue::ContainsCI(name, needle.c_str())) return false;
        const std::string cls = ue::ClassName(obj);
        if (!classFilter.empty() && !ue::ContainsCI(cls, classFilter.c_str())) return false;
        std::uint32_t number = 0;
        ue::SafeReadU32(obj + ue::Layout().kNamePrivate + 4, number);
        Log::Line("dev: find 0x%llx %s %s#%u outer=%s", static_cast<unsigned long long>(obj),
                  cls.c_str(), name.c_str(), number, ue::OuterName(obj).c_str());
        return ++shown >= 300;
    });
    Log::Line("dev: find '%s' class~'%s' -> %d shown", needle.c_str(), classFilter.c_str(), shown);
}

void HexDump(std::uintptr_t addr, std::size_t bytes) {
    for (std::size_t off = 0; off < bytes; off += 8) {
        std::uintptr_t q = 0;
        if (!ue::SafeReadPtr(addr + off, q)) { Log::Line("dev: mem +0x%zx unreadable", off); return; }
        double d = 0.0; std::memcpy(&d, &q, 8);
        float f0 = 0, f1 = 0; std::memcpy(&f0, &q, 4); std::memcpy(&f1, reinterpret_cast<char*>(&q) + 4, 4);
        Log::Line("dev: mem +0x%03zx %016llx  d=%g f=(%g,%g)", off,
                  static_cast<unsigned long long>(q), d, f0, f1);
    }
}

// UE4 offsets the dev verbs read directly. Everything the mod itself relies on
// goes through the build profile's ReflectionLayout or the engine's property
// table; these are here because a throwaway probe is not worth a profile field,
// and named because a bare 0x48 in a walk says nothing about what it walks.
constexpr std::size_t kUStruct_Children = 0x48;        // UField* chain of a class
constexpr std::size_t kUField_Next = 0x28;
constexpr std::size_t kUStruct_ScriptData = 0x60;      // TArray<uint8> bytecode
constexpr std::size_t kUStruct_ScriptNum = 0x68;
constexpr std::size_t kFBoolProperty_ByteOffsets = 0x68;
constexpr std::size_t kInputActionMappingStride = 0x28;  // FInputActionKeyMapping
constexpr std::size_t kInputActionMappingKey = 0x10;

std::uintptr_t g_controller = 0;

std::uintptr_t ReadPtrProp(std::uintptr_t obj, const char* name) {
    ue_reflect::FieldInfo f;
    if (!ue_reflect::FindPropertyInChain(ue_call::ClassOf(obj), name, f)) return 0;
    std::uintptr_t v = 0;
    ue::SafeReadPtr(obj + f.Offset, v);
    return v;
}

std::uintptr_t CallObjectGetter(std::uintptr_t self, const char* outer, const char* fnName) {
    const std::uintptr_t fn = ue::FindLiveObject("Function", fnName, outer);
    if (!fn || !self) return 0;
    std::vector<ue_reflect::FieldInfo> p;
    if (!ue_reflect::ResolveAll(fnName, fn, {"ReturnValue"}, p)) return 0;
    alignas(16) unsigned char buf[512];
    // ProcessEvent fills the frame from the UFunction's real size whatever we
    // measured, so a failed read must not be taken as a zero-size frame.
    std::size_t size = 0;
    if (!ue_reflect::TryStructSize(fn, size) || size > sizeof(buf)) return 0;
    std::memset(buf, 0, size);
    if (!ue_vm::Dispatch(reinterpret_cast<void*>(self), reinterpret_cast<void*>(fn), buf)) return 0;
    std::uintptr_t v = 0;
    if (p[0].Size >= 8) std::memcpy(&v, buf + p[0].Offset, 8);
    return v;
}

std::uintptr_t Target(const std::string& t) {
    if (t == "controller") return g_controller;
    if (t == "pawn") return ReadPtrProp(g_controller, "Pawn");
    if (t == "pcm") return ReadPtrProp(g_controller, "PlayerCameraManager");
    if (t == "weapon") return ReadPtrProp(ReadPtrProp(g_controller, "Pawn"), "CurrentWeapon");
    if (t == "world") return ue::OuterObject(ue::OuterObject(g_controller));
    if (t == "gamestate") return ReadPtrProp(ue::OuterObject(ue::OuterObject(g_controller)), "GameState");
    if (t == "gamemode") return ReadPtrProp(ue::OuterObject(ue::OuterObject(g_controller)), "AuthorityGameMode");
    if (t == "playerstate") return ReadPtrProp(g_controller, "PlayerState");
    if (t == "hud") return ReadPtrProp(g_controller, "MyHUD");
    return std::strtoull(t.c_str(), nullptr, 16);
}

void LogProp(std::uintptr_t obj, const std::string& name) {
    ue_reflect::FieldInfo f;
    if (!obj || !ue_reflect::FindPropertyInChain(ue_call::ClassOf(obj), name.c_str(), f)) {
        Log::Line("dev: prop %s not found on 0x%llx", name.c_str(), static_cast<unsigned long long>(obj));
        return;
    }
    const std::uintptr_t at = obj + f.Offset;
    if (f.TypeName == "ObjectProperty" || f.TypeName == "ClassProperty") {
        std::uintptr_t v = 0; ue::SafeReadPtr(at, v);
        Log::Line("dev: %s.%s (+0x%zx) = 0x%llx %s %s", ue::ClassName(obj).c_str(), name.c_str(), f.Offset,
                  static_cast<unsigned long long>(v), v ? ue::ClassName(v).c_str() : "", v ? ue::ObjectName(v).c_str() : "");
    } else if (f.Size == 12 && f.TypeName == "StructProperty") {
        ue4::FVector v{}; cameraunlock::memory::SafeRead(at, v);
        Log::Line("dev: %s.%s (+0x%zx) = (%.4f, %.4f, %.4f)", ue::ClassName(obj).c_str(), name.c_str(), f.Offset, v.X, v.Y, v.Z);
    } else if (f.TypeName == "FloatProperty") {
        float v = 0; ue::SafeReadFloat(at, v);
        Log::Line("dev: %s.%s (+0x%zx) = %f", ue::ClassName(obj).c_str(), name.c_str(), f.Offset, v);
    } else {
        std::uintptr_t q = 0; ue::SafeReadPtr(at, q);
        Log::Line("dev: %s.%s (+0x%zx) %s size %zu raw=%016llx", ue::ClassName(obj).c_str(), name.c_str(), f.Offset,
                  f.TypeName.c_str(), f.Size, static_cast<unsigned long long>(q));
    }
}

void LogComponentToWorld(std::uintptr_t comp) {
    const std::uintptr_t fn = ue::FindLiveObject("Function", "K2_GetComponentToWorld", "SceneComponent");
    const std::uintptr_t ts = ue::FindLiveObject("ScriptStruct", "Transform", nullptr);
    std::vector<ue_reflect::FieldInfo> p, t;
    if (!comp || !fn || !ts || !ue_reflect::ResolveAll("K2_GetComponentToWorld", fn, {"ReturnValue"}, p) ||
        !ue_reflect::ResolveAll("Transform", ts, {"Rotation", "Translation", "Scale3D"}, t)) {
        Log::Line("dev: ctw unavailable");
        return;
    }
    alignas(16) unsigned char buf[512];
    if (!FrameFits("K2_GetComponentToWorld", fn, sizeof(buf)) ||
        !ue_reflect::FieldFits(p[0], ue_reflect::StructSize(ts), ue_reflect::StructSize(fn)))
        return;
    std::memset(buf, 0, sizeof(buf));
    if (!ue_vm::Dispatch(reinterpret_cast<void*>(comp), reinterpret_cast<void*>(fn), buf)) return;
    ue4::FQuat q{}; ue4::FVector tr{};
    std::memcpy(&q, buf + p[0].Offset + t[0].Offset, sizeof(q));
    std::memcpy(&tr, buf + p[0].Offset + t[1].Offset, sizeof(tr));
    const ue::FVector fwd = ue::QuatRotateVec(ue4::ToCore(q), ue::FVector{1, 0, 0});
    Log::Line("dev: ctw %s pos=(%.2f, %.2f, %.2f) fwd=(%.5f, %.5f, %.5f) [rotOff=+0x%zx trOff=+0x%zx]",
              ue::ObjectName(comp).c_str(), tr.X, tr.Y, tr.Z, fwd.X, fwd.Y, fwd.Z, t[0].Offset, t[1].Offset);
}

struct Arg { const char* Name; const void* Data; std::size_t Bytes; };

bool CallWithArgs(std::uintptr_t self, const char* outer, const char* fnName,
                  std::initializer_list<Arg> args, std::vector<unsigned char>& frame,
                  std::vector<ue_reflect::FieldInfo>* fieldsOut = nullptr);

std::uint64_t ToFName(const std::string& text) {
    const std::wstring wide = Widen(text);
    const FStringHeader str{wide.c_str(), static_cast<std::int32_t>(wide.size() + 1),
                            static_cast<std::int32_t>(wide.size() + 1)};
    std::vector<unsigned char> conv;
    std::vector<ue_reflect::FieldInfo> convFields;
    const std::uintptr_t strLib = ue::FindLiveObject("KismetStringLibrary", "Default__KismetStringLibrary", nullptr);
    if (!CallWithArgs(strLib, "KismetStringLibrary", "Conv_StringToName", {{"inString", &str, sizeof(str)}},
                      conv, &convFields))
        return 0;
    std::uint64_t name = 0;
    for (const auto& f : convFields)
        if (f.Name == "ReturnValue") std::memcpy(&name, conv.data() + f.Offset, 8);
    return name;
}

// Dispatch a UFUNCTION with named arguments; returns the frame for reading.
bool CallWithArgs(std::uintptr_t self, const char* outer, const char* fnName,
                  std::initializer_list<Arg> args, std::vector<unsigned char>& frame,
                  std::vector<ue_reflect::FieldInfo>* fieldsOut) {
    const std::uintptr_t fn = std::strncmp(fnName, "0x", 2) == 0 ? std::strtoull(fnName, nullptr, 16)
                                                                 : ue::FindLiveObject("Function", fnName, outer);
    if (!fn || !self) { Log::Line("dev: %s::%s unavailable", outer, fnName); return false; }
    // The read has to be told apart from the answer: a parameterless function
    // legitimately measures zero, but so does a failed read, and ProcessEvent
    // fills the function's real frame either way - so dispatching on a failed
    // read hands it a 16-byte buffer to write the real frame into.
    std::size_t size = 0;
    if (!ue_reflect::TryStructSize(fn, size) || size > 4096) {
        Log::Line("dev: %s::%s parameter frame unreadable or too large - not called", outer, fnName);
        return false;
    }
    const auto fields = ue_reflect::Properties(fn);
    frame.assign(size + 16, 0);
    for (const Arg& a : args) {
        bool placed = false;
        for (const auto& f : fields) {
            if (f.Name != a.Name) continue;
            if (!ue_reflect::FieldFits(f, a.Bytes, size)) break;
            std::memcpy(frame.data() + f.Offset, a.Data, a.Bytes);
            placed = true;
            break;
        }
        if (!placed) { Log::Line("dev: %s::%s has no slot for %s", outer, fnName, a.Name); return false; }
    }
    if (fieldsOut) *fieldsOut = fields;
    return ue_vm::Dispatch(reinterpret_cast<void*>(self), reinterpret_cast<void*>(fn), frame.data());
}

bool ActorLocation(std::uintptr_t actor, ue4::FVector& out) {
    std::vector<unsigned char> frame;
    std::vector<ue_reflect::FieldInfo> fields;
    if (!CallWithArgs(actor, "Actor", "K2_GetActorLocation", {}, frame, &fields)) return false;
    for (const auto& f : fields)
        if (f.Name == "ReturnValue") { std::memcpy(&out, frame.data() + f.Offset, sizeof(out)); return true; }
    return false;
}

void ListActors(const std::string& needle) {
    std::vector<std::uintptr_t> hits;
    ue::ForEachUObject([&](std::uintptr_t obj) {
        const std::string name = ue::ObjectName(obj);
        if (name.rfind("Default__", 0) == 0) return false;
        const std::uintptr_t outer = ue::OuterObject(obj);
        if (!outer || ue::ClassName(outer) != "Level") return false;
        if (!ue::ContainsCI(name, needle.c_str()) && !ue::ContainsCI(ue::ClassName(obj), needle.c_str())) return false;
        hits.push_back(obj);
        return hits.size() >= 120;
    });
    for (std::uintptr_t obj : hits) {
        ue4::FVector loc{};
        const bool ok = ActorLocation(obj, loc);
        Log::Line("dev: actor 0x%llx %s %s %s (%.1f, %.1f, %.1f)", static_cast<unsigned long long>(obj),
                  ue::ClassName(obj).c_str(), ue::ObjectName(obj).c_str(), ok ? "at" : "(no location)",
                  loc.X, loc.Y, loc.Z);
    }
}

void CmdExec(const std::string& rest, std::uintptr_t controller) {
    if (!ExecuteConsoleCommand(controller, Widen(rest)))
        Log::Line("dev: exec failed");
    else
        Log::Line("dev: exec dispatched");
}

void CmdOpenLevel(const std::string& rest, std::uintptr_t controller) {
    const std::uint64_t name = ToFName(rest);
    const std::uintptr_t statics = ue::FindLiveObject("GameplayStatics", "Default__GameplayStatics", nullptr);
    const unsigned char absolute = 1;
    std::vector<unsigned char> frame;
    const bool ok = name && CallWithArgs(statics, "GameplayStatics", "OpenLevel",
                                         {{"WorldContextObject", &controller, 8}, {"LevelName", &name, 8},
                                          {"bAbsolute", &absolute, 1}}, frame);
    Log::Line("dev: openlevel %s name=0x%llx %s", rest.c_str(), static_cast<unsigned long long>(name),
              ok ? "dispatched" : "failed");
}

void CmdActionMaps(const std::string& rest, std::uintptr_t) {
    const std::uintptr_t settings = rest.empty()
        ? ue::FindLiveObject("InputSettings", "Default__InputSettings", nullptr)
        : ReadPtrProp(g_controller, "PlayerInput");
    ue_reflect::FieldInfo f;
    if (!settings || !ue_reflect::FindPropertyInChain(ue_call::ClassOf(settings), "ActionMappings", f)) return;
    std::uintptr_t data = 0; std::uint32_t num = 0;
    ue::SafeReadPtr(settings + f.Offset, data);
    ue::SafeReadU32(settings + f.Offset + 8, num);
    for (std::uint32_t i = 0; i < num && i < 400; ++i) {
        std::uint32_t action = 0, key = 0;
        ue::SafeReadU32(data + i * kInputActionMappingStride, action);
        ue::SafeReadU32(data + i * kInputActionMappingStride + kInputActionMappingKey, key);
        Log::Line("dev: action %s <- %s", ue::ResolveFName(action).c_str(), ue::ResolveFName(key).c_str());
    }
}

void CmdBindKey(const std::string& rest, std::uintptr_t) {
    // bindkey <ActionName> <KeyName>: add an action mapping through UInputSettings.
    std::istringstream r(rest);
    std::string action, key;
    r >> action >> key;
    const std::uintptr_t settings = ue::FindLiveObject("InputSettings", "Default__InputSettings", nullptr);
    unsigned char mapping[kInputActionMappingStride] = {};
    const std::uint64_t actionName = ToFName(action), keyName = ToFName(key);
    std::memcpy(mapping, &actionName, 8);
    std::memcpy(mapping + kInputActionMappingKey, &keyName, 8);
    const unsigned char rebuild = 1;
    std::vector<unsigned char> frame;
    const bool ok = actionName && keyName &&
                    CallWithArgs(settings, "InputSettings", "AddActionMapping",
                                 {{"KeyMapping", mapping, sizeof(mapping)}, {"bForceRebuildKeymaps", &rebuild, 1}},
                                 frame);
    Log::Line("dev: bindkey %s <- %s %s", action.c_str(), key.c_str(), ok ? "ok" : "failed");
}

void CmdSetFloat(const std::string& rest, std::uintptr_t) {
    std::istringstream r(rest);
    std::string t, n;
    float v = 0;
    // A failed extraction stores 0, so an unparseable value would write a zero
    // into the game's float and report it as a successful set.
    if (!(r >> t >> n >> v)) { Log::Line("dev: setf wants <target> <field> <number>"); return; }
    const std::uintptr_t obj = Target(t);
    ue_reflect::FieldInfo f;
    if (obj && ue_reflect::FindPropertyInChain(ue_call::ClassOf(obj), n.c_str(), f) && f.TypeName == "FloatProperty") {
        const bool wrote = ue::SafeWriteFloat(obj + f.Offset, v);
        Log::Line("dev: setf %s=%g%s", n.c_str(), v, wrote ? "" : " (unwritable)");
    }
}

void CmdClass(const std::string& rest, std::uintptr_t) {
    const std::uintptr_t cls = ue::FindLiveObject("Class", rest.c_str(), nullptr);
    if (!cls) { Log::Line("dev: no class %s", rest.c_str()); return; }
    DumpClassChain("class", cls);
}

void CmdStruct(const std::string& rest, std::uintptr_t) {
    const std::uintptr_t s = ue::FindLiveObject("ScriptStruct", rest.c_str(), nullptr);
    if (!s) { Log::Line("dev: no struct %s", rest.c_str()); return; }
    ue_reflect::DumpProperties(rest.c_str(), s);
}

void CmdFunc(const std::string& rest, std::uintptr_t) {
    std::istringstream r(rest);
    std::string outer, fn;
    r >> outer >> fn;
    const std::uintptr_t f = ue::FindLiveObject("Function", fn.c_str(), outer.c_str());
    if (!f) { Log::Line("dev: no function %s::%s", outer.c_str(), fn.c_str()); return; }
    ue_reflect::DumpProperties(fn.c_str(), f);
}

void CmdFind(const std::string& rest, std::uintptr_t) {
    std::istringstream r(rest);
    std::string needle, cls;
    r >> needle >> cls;
    FindObjects(needle, cls);
}

void CmdMem(const std::string& rest, std::uintptr_t controller) {
    std::istringstream r(rest);
    std::string a;
    std::size_t n = 0x100;
    // Extract the count separately: a failed read stores 0, which would replace
    // the documented default with an empty dump.
    if (!(r >> a)) { Log::Line("dev: mem wants <address|controller> [count]"); return; }
    std::size_t count = 0;
    if (r >> std::hex >> count) n = count;
    std::uintptr_t addr = a == "controller" ? controller : std::strtoull(a.c_str(), nullptr, 16);
    HexDump(addr, n);
}

void CmdName(const std::string& rest, std::uintptr_t) {
    const std::uintptr_t obj = std::strtoull(rest.c_str(), nullptr, 16);
    Log::Line("dev: 0x%llx = %s %s outer=%s", static_cast<unsigned long long>(obj),
              ue::ClassName(obj).c_str(), ue::ObjectName(obj).c_str(), ue::OuterName(obj).c_str());
}

void CmdProp(const std::string& rest, std::uintptr_t) {
    std::istringstream r(rest);
    std::string t, n;
    while (r >> t >> n) LogProp(Target(t), n);
}

void CmdCall(const std::string& rest, std::uintptr_t) {
    std::istringstream r(rest);
    std::string t, outer, fn;
    r >> t >> outer >> fn;
    const std::uintptr_t v = CallObjectGetter(Target(t), outer.c_str(), fn.c_str());
    Log::Line("dev: %s::%s -> 0x%llx %s %s", outer.c_str(), fn.c_str(), static_cast<unsigned long long>(v),
              v ? ue::ClassName(v).c_str() : "", v ? ue::ObjectName(v).c_str() : "");
}

void CmdComponentToWorld(const std::string& rest, std::uintptr_t) {
    std::istringstream r(rest);
    std::string t, n;
    r >> t >> n;
    LogComponentToWorld(n.empty() ? Target(t) : ReadPtrProp(Target(t), n.c_str()));
}

void CmdStructOf(const std::string& rest, std::uintptr_t) {
    // structof <target> <StructProperty name>: dump the struct type and the
    // instance's float/int fields.
    std::istringstream r(rest);
    std::string t, n;
    r >> t >> n;
    const std::uintptr_t obj = Target(t);
    ue_reflect::FieldInfo f;
    if (!obj || !ue_reflect::FindPropertyInChain(ue_call::ClassOf(obj), n.c_str(), f)) {
        Log::Line("dev: structof: no %s", n.c_str());
        return;
    }
    const std::uintptr_t st = ue_reflect::StructOf(f);
    ue_reflect::DumpProperties(ue::ObjectName(st).c_str(), st);
    for (const auto& p : ue_reflect::Properties(st)) {
        float fv = 0; std::int32_t iv = 0; std::uint8_t bv = 0;
        ue::SafeReadFloat(obj + f.Offset + p.Offset, fv);
        ue::SafeReadU32(obj + f.Offset + p.Offset, reinterpret_cast<std::uint32_t&>(iv));
        cameraunlock::memory::SafeRead(obj + f.Offset + p.Offset, bv);
        Log::Line("dev:   %s = f%g i%d b%u", p.Name.c_str(), fv, iv, bv);
    }
}

void CmdClassOf(const std::string& rest, std::uintptr_t) {
    DumpClassChain("classof", ue_call::ClassOf(Target(rest)));
}

void CmdArray(const std::string& rest, std::uintptr_t) {
    std::istringstream r(rest);
    std::string t, n;
    r >> t >> n;
    const std::uintptr_t obj = Target(t);
    ue_reflect::FieldInfo f;
    if (!obj || !ue_reflect::FindPropertyInChain(ue_call::ClassOf(obj), n.c_str(), f)) return;
    std::uintptr_t data = 0; std::uint32_t num = 0;
    ue::SafeReadPtr(obj + f.Offset, data);
    ue::SafeReadU32(obj + f.Offset + 8, num);
    Log::Line("dev: arr %s.%s num=%u data=0x%llx", t.c_str(), n.c_str(), num, static_cast<unsigned long long>(data));
    for (std::uint32_t i = (num > 8 ? num - 8 : 0); i < num && data; ++i) {
        std::uintptr_t e = 0;
        ue::SafeReadPtr(data + i * 8, e);
        ue4::FVector loc{};
        const bool actor = e && ue::ClassName(ue::OuterObject(e)) == "Level" && ActorLocation(e, loc);
        Log::Line("dev: arr[%u] 0x%llx %s %s %s (%.2f, %.2f, %.2f)", i, static_cast<unsigned long long>(e),
                  e ? ue::ClassName(e).c_str() : "", e ? ue::ObjectName(e).c_str() : "",
                  actor ? "at" : "", loc.X, loc.Y, loc.Z);
    }
}

void CmdMarkPoint(const std::string& rest, std::uintptr_t) {
    std::istringstream r(rest);
    double x = 0, y = 0, z = 0;
    if (r >> x >> y >> z) view_hook::SetMarkOverride(true, x, y, z);
    else view_hook::SetMarkOverride(false, 0, 0, 0);
    Log::Line("dev: markpoint %s", rest.c_str());
}

void CmdAim(const std::string&, std::uintptr_t) {
    const view_hook::AimSample a = view_hook::LastAim();
    Log::Line("dev: aim origin=(%.3f,%.3f,%.3f) dir=(%.6f,%.6f,%.6f) hit=%d point=(%.2f,%.2f,%.2f) "
              "eye=(%.3f,%.3f,%.3f) ndc=(%.4f,%.4f) valid=%d",
              a.MuzzleX, a.MuzzleY, a.MuzzleZ, a.DirX, a.DirY, a.DirZ, a.Hit ? 1 : 0, a.PointX, a.PointY,
              a.PointZ, a.EyeX, a.EyeY, a.EyeZ, a.NdcX, a.NdcY, a.NdcValid ? 1 : 0);
}

void CmdScriptRefs(const std::string& rest, std::uintptr_t) {
    // scriptrefs <outer> <function>: every blueprint function whose bytecode
    // holds a pointer to that UFunction (UStruct::Script is TArray<uint8> at +0x60).
    std::istringstream r(rest);
    std::string outer, fn;
    r >> outer >> fn;
    const std::uintptr_t target = ue::FindLiveObject("Function", fn.c_str(), outer.c_str());
    if (!target) { Log::Line("dev: scriptrefs no %s::%s", outer.c_str(), fn.c_str()); return; }
    int hits = 0;
    ue::ForEachUObject([&](std::uintptr_t obj) {
        if (ue::ClassName(obj) != "Function") return false;
        std::uintptr_t data = 0;
        std::uint32_t num = 0;
        if (!ue::SafeReadPtr(obj + kUStruct_ScriptData, data) ||
            !ue::SafeReadU32(obj + kUStruct_ScriptNum, num) || !data || num < 8 ||
            num > 0x100000)
            return false;
        std::vector<unsigned char> bytes(num);
        for (std::uint32_t i = 0; i < num; ++i)
            if (!cameraunlock::memory::SafeRead(data + i, bytes[i])) return false;
        for (std::uint32_t i = 0; i + 8 <= num; ++i) {
            std::uintptr_t v = 0;
            std::memcpy(&v, bytes.data() + i, 8);
            if (v != target) continue;
            Log::Line("dev: scriptrefs %s::%s at +%u", ue::OuterName(obj).c_str(), ue::ObjectName(obj).c_str(), i);
            ++hits;
            break;
        }
        return false;
    });
    Log::Line("dev: scriptrefs %s::%s -> %d functions", outer.c_str(), fn.c_str(), hits);
}

void CmdWidgetTree(const std::string& rest, std::uintptr_t) {
    // wtree <widget address>: parent chain, then every widget under the
    // top-most parent, with visibility and render translation.
    std::uintptr_t w = std::strtoull(rest.c_str(), nullptr, 16);
    const std::uintptr_t parentFn = ue::FindLiveObject("Function", "GetParent", "Widget");
    const std::uintptr_t countFn = ue::FindLiveObject("Function", "GetChildrenCount", "PanelWidget");
    const std::uintptr_t childFn = ue::FindLiveObject("Function", "GetChildAt", "PanelWidget");
    const std::uintptr_t visFn = ue::FindLiveObject("Function", "GetVisibility", "Widget");
    const std::uintptr_t widgetClass = ue::FindLiveObject("Class", "Widget", nullptr);
    ue_reflect::FieldInfo xf;
    if (!parentFn || !countFn || !childFn || !visFn || !widgetClass ||
        !ue_reflect::FindPropertyInChain(widgetClass, "RenderTransform", xf)) {
        Log::Line("dev: wtree unavailable");
        return;
    }
    auto call1 = [](std::uintptr_t self, std::uintptr_t fn, const unsigned char* in, std::size_t inBytes,
                    std::size_t outOffset, std::size_t outBytes, unsigned char* out) {
        alignas(16) unsigned char frame[64] = {};
        if (!FrameFits("wtree", fn, sizeof(frame)) || outOffset + outBytes > sizeof(frame))
            return false;
        if (in) std::memcpy(frame, in, inBytes);
        if (!ue_vm::Dispatch(reinterpret_cast<void*>(self), reinterpret_cast<void*>(fn), frame)) return false;
        std::memcpy(out, frame + outOffset, outBytes);
        return true;
    };
    auto describe = [&](std::uintptr_t x, int depth) {
        unsigned char vis = 0xff;
        call1(x, visFn, nullptr, 0, 0, 1, &vis);
        float tx = 0, ty = 0;
        ue::SafeReadFloat(x + xf.Offset, tx);
        ue::SafeReadFloat(x + xf.Offset + 4, ty);
        Log::Line("dev: wtree %*s0x%llx %s %s vis=%u translation=(%.1f,%.1f)", depth * 2, "",
                  static_cast<unsigned long long>(x), ue::ClassName(x).c_str(), ue::ObjectName(x).c_str(),
                  vis, tx, ty);
    };
    std::uintptr_t top = w;
    for (int i = 0; i < 12; ++i) {
        describe(top, 0);
        std::uintptr_t parent = 0;
        unsigned char out[8] = {};
        if (!call1(top, parentFn, nullptr, 0, 0, 8, out)) break;
        std::memcpy(&parent, out, 8);
        if (!parent) break;
        top = parent;
    }
    std::function<void(std::uintptr_t, int)> walk = [&](std::uintptr_t x, int depth) {
        describe(x, depth);
        if (depth > 6 || !ue_call::IsA(x, "PanelWidget")) return;
        std::int32_t count = 0;
        unsigned char out[8] = {};
        if (!call1(x, countFn, nullptr, 0, 0, 4, out)) return;
        std::memcpy(&count, out, 4);
        for (std::int32_t i = 0; i < count && i < 64; ++i) {
            unsigned char in[8] = {};
            std::memcpy(in, &i, 4);
            std::uintptr_t child = 0;
            if (!call1(x, childFn, in, 4, 8, 8, out)) break;
            std::memcpy(&child, out, 8);
            if (child) walk(child, depth + 1);
        }
    };
    Log::Line("dev: wtree children of top 0x%llx", static_cast<unsigned long long>(top));
    walk(top, 0);
}

void CmdGiveWeapon(const std::string& rest, std::uintptr_t) {
    // giveweapon <BlueprintGeneratedClass address>: spawn a weapon at the pawn and equip it.
    const std::uintptr_t cls = std::strtoull(rest.c_str(), nullptr, 16);
    const std::uintptr_t pawn = Target("pawn");
    const std::uintptr_t statics = ue::FindLiveObject("GameplayStatics", "Default__GameplayStatics", nullptr);
    ue4::FVector at{};
    ActorLocation(pawn, at);
    alignas(16) unsigned char xf[0x30] = {};
    const float quat[4] = {0, 0, 0, 1}, scale[3] = {1, 1, 1};
    std::memcpy(xf, quat, sizeof(quat));
    std::memcpy(xf + 0x10, &at, sizeof(at));
    std::memcpy(xf + 0x20, scale, sizeof(scale));
    const unsigned char always = 1;  // ESpawnActorCollisionHandlingMethod::AlwaysSpawn
    std::vector<unsigned char> frame;
    std::vector<ue_reflect::FieldInfo> fields;
    if (!cls || !pawn ||
        !CallWithArgs(statics, "GameplayStatics", "BeginDeferredActorSpawnFromClass",
                      {{"WorldContextObject", &pawn, 8}, {"ActorClass", &cls, 8}, {"SpawnTransform", xf, sizeof(xf)},
                       {"CollisionHandlingOverride", &always, 1}}, frame, &fields)) {
        Log::Line("dev: giveweapon spawn failed");
        return;
    }
    std::uintptr_t actor = 0;
    for (const auto& f : fields)
        if (f.Name == "ReturnValue") std::memcpy(&actor, frame.data() + f.Offset, 8);
    std::vector<unsigned char> finish;
    CallWithArgs(statics, "GameplayStatics", "FinishSpawningActor",
                 {{"Actor", &actor, 8}, {"SpawnTransform", xf, sizeof(xf)}}, finish);
    const unsigned char no = 0, yes = 1;
    std::vector<unsigned char> add;
    const bool ok = actor && CallWithArgs(pawn, "BaseCharacter", "AddWeapon",
                                          {{"Weapon", &actor, 8}, {"ReplaceWithDuplicate", &no, 1},
                                           {"bForceEquip", &yes, 1}}, add);
    Log::Line("dev: giveweapon %s -> 0x%llx %s", ue::ObjectName(cls).c_str(),
              static_cast<unsigned long long>(actor), ok ? "added" : "failed");
}

void CmdShotLog(const std::string&, std::uintptr_t) {
    InstallShotLog();
}

void CmdAdsMode(const std::string& rest, std::uintptr_t) {
    SetAdsMode(ParseAdsMode(rest.c_str()));
    Log::Line("dev: ads mode %s", AdsModeValue(GetAdsMode()));
}

void CmdInject(const std::string& rest, std::uintptr_t) {
    long mode = 0;
    if (!ParseInt(rest, mode)) {
        Log::Line("dev: inject '%s' is not a mode number", rest.c_str());
        return;
    }
    view_hook::SetInjectMode(static_cast<int>(mode));
    Log::Line("dev: inject mode %d", view_hook::InjectMode());
}

void CmdReport(const std::string&, std::uintptr_t) {
    view_hook::RequestReport();
}

void CmdTracking(const std::string& rest, std::uintptr_t) {
    view_hook::SetTrackingEnabled(rest == "1");
    Log::Line("dev: tracking %s", rest == "1" ? "on" : "off");
}

void CmdProcessEventTrace(const std::string& rest, std::uintptr_t) {
    std::istringstream r(rest);
    std::string t; int ms = 1000;
    if (!(r >> t)) { Log::Line("dev: petrace wants <target|all> [ms]"); return; }
    int given = 0;
    if (r >> given) ms = given;
    g_peTarget = t == "all" ? 0 : Target(t);
    { std::lock_guard<std::mutex> lk(g_peMutex); g_peSeen.clear(); }
    g_peUntil = GetTickCount64() + static_cast<std::uint64_t>(ms);
    process_event_hook::Install();
    process_event_hook::SetObserver(&PeObserver);
    Log::Line("dev: petrace on %s for %d ms", t.c_str(), ms);
}

void CmdFuncs(const std::string& rest, std::uintptr_t) {
    std::uintptr_t cls = ue::FindLiveObject("Class", rest.c_str(), nullptr);
    if (!cls) cls = ue::FindLiveObject("BlueprintGeneratedClass", rest.c_str(), nullptr);
    if (!cls) cls = ue::FindLiveObject("WidgetBlueprintGeneratedClass", rest.c_str(), nullptr);
    if (!cls && Target(rest)) cls = ue_call::ClassOf(Target(rest));
    if (!cls) { Log::Line("dev: funcs no class %s", rest.c_str()); return; }
    for (int depth = 0; cls && depth < 8; ++depth) {
        std::uintptr_t child = 0;
        ue::SafeReadPtr(cls + kUStruct_Children, child);
        std::string names;
        for (int i = 0; child && i < 2000; ++i) {
            if (ue::ClassName(child) == "Function") names += ue::ObjectName(child) + " ";
            if (names.size() > 700) {
                Log::Line("dev: funcs %s: %s", ue::ObjectName(cls).c_str(), names.c_str());
                names.clear();
            }
            if (!ue::SafeReadPtr(child + kUField_Next, child)) break;
        }
        Log::Line("dev: funcs %s: %s", ue::ObjectName(cls).c_str(), names.c_str());
        if (!ue::SafeReadPtr(cls + Offsets().Reflection.kUStruct_SuperStruct, cls)) break;
    }
}

void CmdInvokeWithParams(const std::string& rest, std::uintptr_t) {
    // invokep <target> <outer> <fn> name=b:1 name=f:1.5 name=v:x,y,z name=p:pawn
    std::istringstream r(rest);
    std::string t, outer, fn, tok;
    r >> t >> outer >> fn;
    struct Owned { std::string Name; std::vector<unsigned char> Bytes; };
    std::vector<Owned> owned;
    while (r >> tok) {
        const auto eq = tok.find('='), colon = tok.find(':');
        if (eq == std::string::npos || colon == std::string::npos) continue;
        Owned o{tok.substr(0, eq), {}};
        const char type = tok[eq + 1];
        const std::string val = tok.substr(colon + 1);
        auto put = [&o](const void* p, std::size_t n) {
            o.Bytes.assign(static_cast<const unsigned char*>(p), static_cast<const unsigned char*>(p) + n);
        };
        long whole = 0;
        double real = 0.0;
        if ((type == 'b' || type == 'i') && !ParseInt(val, whole)) {
            Log::Line("dev: invokep %s=%s is not a whole number", o.Name.c_str(), val.c_str());
            continue;
        }
        if ((type == 'f' || type == 'd') && !ParseDouble(val, real)) {
            Log::Line("dev: invokep %s=%s is not a number", o.Name.c_str(), val.c_str());
            continue;
        }
        if (type == 'b') { unsigned char b = static_cast<unsigned char>(whole); put(&b, 1); }
        else if (type == 'i') { std::int32_t i = static_cast<std::int32_t>(whole); put(&i, 4); }
        else if (type == 'f') { float f = static_cast<float>(real); put(&f, 4); }
        else if (type == 'd') { double d = real; put(&d, 8); }
        else if (type == 'r') {
            ue4::FRotator v{}; std::sscanf(val.c_str(), "%f,%f,%f", &v.Pitch, &v.Yaw, &v.Roll); put(&v, sizeof(v));
        }
        else if (type == 'p') { std::uintptr_t p = Target(val); put(&p, 8); }
        else if (type == 'v') {
            ue4::FVector v{}; std::sscanf(val.c_str(), "%f,%f,%f", &v.X, &v.Y, &v.Z); put(&v, sizeof(v));
        }
        else {
            Log::Line("dev: invokep %s has unknown type '%c' (b i f d r p v)", o.Name.c_str(), type);
            continue;
        }
        owned.push_back(o);
    }
    const std::uintptr_t fnObj = ue::FindLiveObject("Function", fn.c_str(), outer.c_str());
    const std::uintptr_t self = Target(t);
    if (!fnObj || !self) { Log::Line("dev: invokep %s::%s unavailable", outer.c_str(), fn.c_str()); return; }
    std::size_t size = 0;
    if (!ue_reflect::TryStructSize(fnObj, size) || size > 4096) {
        Log::Line("dev: invokep %s::%s parameter frame unreadable or too large - not called",
                  outer.c_str(), fn.c_str());
        return;
    }
    const auto fields = ue_reflect::Properties(fnObj);
    std::vector<unsigned char> frame(size + 16, 0);
    for (const Owned& o : owned) {
        bool placed = false;
        for (const auto& f : fields) {
            if (f.Name != o.Name || !ue_reflect::FieldFits(f, o.Bytes.size(), size)) continue;
            if (f.BoolMask) { if (o.Bytes[0]) frame[f.Offset] |= f.BoolMask; }
            else std::memcpy(frame.data() + f.Offset, o.Bytes.data(), o.Bytes.size());
            placed = true;
        }
        if (!placed) Log::Line("dev: invokep no slot %s", o.Name.c_str());
    }
    const bool ok = ue_vm::Dispatch(reinterpret_cast<void*>(self), reinterpret_cast<void*>(fnObj), frame.data());
    std::string ret;
    for (const auto& f : fields) {
        if (f.Name != "ReturnValue") continue;
        char b[96];
        std::uint64_t q = 0; std::memcpy(&q, frame.data() + f.Offset, f.Size < 8 ? f.Size : 8);
        std::snprintf(b, sizeof(b), " ReturnValue=0x%llx", static_cast<unsigned long long>(q));
        ret = b;
    }
    Log::Line("dev: invokep %s::%s %s%s", outer.c_str(), fn.c_str(), ok ? "ok" : "failed", ret.c_str());
}

void CmdSetBool(const std::string& rest, std::uintptr_t) {
    std::istringstream r(rest);
    std::string t, n; int v = 0;
    // As in setf: a failed extraction reads as 0, which silently clears the bit
    // the caller asked to set.
    if (!(r >> t >> n >> v)) { Log::Line("dev: setb wants <target> <field> <0|1>"); return; }
    const std::uintptr_t obj = Target(t);
    ue_reflect::FieldInfo f;
    if (obj && ue_reflect::FindPropertyInChain(ue_call::ClassOf(obj), n.c_str(), f) && f.BoolMask) {
        std::uint8_t b = 0;
        if (!cameraunlock::memory::SafeReadU8(obj + f.Offset, b)) {
            Log::Line("dev: setb %s unreadable", n.c_str());
            return;
        }
        b = v ? static_cast<std::uint8_t>(b | f.BoolMask)
              : static_cast<std::uint8_t>(b & ~f.BoolMask);
        const bool wrote = cameraunlock::memory::SafeWrite(obj + f.Offset, b);
        Log::Line("dev: setb %s=%d%s", n.c_str(), v, wrote ? "" : " (unwritable)");
    }
}

void CmdInvoke(const std::string& rest, std::uintptr_t) {
    std::istringstream r(rest);
    std::string t, outer, fn;
    r >> t >> outer >> fn;
    std::vector<unsigned char> frame;
    const bool ok = CallWithArgs(Target(t), outer.c_str(), fn.c_str(), {}, frame);
    Log::Line("dev: invoke %s::%s %s", outer.c_str(), fn.c_str(), ok ? "ok" : "failed");
}

void CmdActors(const std::string& rest, std::uintptr_t) {
    ListActors(rest);
}

void CmdTeleport(const std::string& rest, std::uintptr_t) {
    std::istringstream r(rest);
    ue4::FVector v{};
    // Partial coordinates would read as 0 and teleport the pawn somewhere the
    // caller did not ask for.
    if (!(r >> v.X >> v.Y >> v.Z)) { Log::Line("dev: tp wants <x> <y> <z>"); return; }
    const unsigned char no = 0, yes = 1;
    std::vector<unsigned char> frame;
    const bool ok = CallWithArgs(Target("pawn"), "Actor", "K2_SetActorLocation",
                                 {{"NewLocation", &v, sizeof(v)}, {"bSweep", &no, 1}, {"bTeleport", &yes, 1}}, frame);
    Log::Line("dev: tp (%.1f, %.1f, %.1f) %s", v.X, v.Y, v.Z, ok ? "ok" : "failed");
}

void CmdFace(const std::string& rest, std::uintptr_t controller) {
    std::istringstream r(rest);
    ue4::FRotator rot{};
    if (!(r >> rot.Yaw >> rot.Pitch)) { Log::Line("dev: face wants <yaw> <pitch>"); return; }
    std::vector<unsigned char> frame;
    const bool ok = CallWithArgs(controller, "Controller", "SetControlRotation",
                                 {{"NewRotation", &rot, sizeof(rot)}}, frame);
    Log::Line("dev: face yaw=%.1f pitch=%.1f %s", rot.Yaw, rot.Pitch, ok ? "ok" : "failed");
}

void CmdLocation(const std::string& rest, std::uintptr_t) {
    ue4::FVector v{};
    const std::uintptr_t a = Target(rest);
    const bool ok = ActorLocation(a, v);
    Log::Line("dev: loc %s = (%.2f, %.2f, %.2f) %s", rest.c_str(), v.X, v.Y, v.Z, ok ? "" : "failed");
}

void CmdGetBool(const std::string& rest, std::uintptr_t) {
    std::istringstream r(rest);
    std::string t, n;
    while (r >> t >> n) {
        const std::uintptr_t obj = Target(t);
        ue_reflect::FieldInfo f;
        bool v = false;
        const bool ok = obj && ue_reflect::FindPropertyInChain(ue_call::ClassOf(obj), n.c_str(), f) &&
                        ue_reflect::ReadBool(obj, f, v);
        Log::Line("dev: getb %s.%s = %s", t.c_str(), n.c_str(), ok ? (v ? "true" : "false") : "unreadable");
    }
}

void CmdBoolMask(const std::string& rest, std::uintptr_t) {
    std::istringstream r(rest);
    std::string t, n;
    r >> t >> n;
    const std::uintptr_t obj = Target(t);
    ue_reflect::FieldInfo f;
    if (obj && ue_reflect::FindPropertyInChain(ue_call::ClassOf(obj), n.c_str(), f)) {
        std::uintptr_t a = 0, b = 0;
        ue::SafeReadPtr(f.Field + kFBoolProperty_ByteOffsets, a);
        ue::SafeReadPtr(f.Field + kFBoolProperty_ByteOffsets + sizeof(std::uintptr_t), b);
        std::uint16_t v = 0;
        ue::SafeReadU16(obj + f.Offset, v);
        Log::Line("dev: bool %s +0x%zx field+0x68=%016llx field+0x70=%016llx value byte=%02x",
                  n.c_str(), f.Offset, static_cast<unsigned long long>(a), static_cast<unsigned long long>(b), v & 0xff);
    }
}

// Every dev verb, one entry per verb and one function per entry. Lookup is a
// walk of the table, so the order is the order they were written in and nothing
// depends on it.
struct Verb {
    const char* Name;
    void (*Run)(const std::string& rest, std::uintptr_t controller);
};

constexpr Verb kVerbs[] = {
    {"exec", &CmdExec},
    {"openlevel", &CmdOpenLevel},
    {"actionmaps", &CmdActionMaps},
    {"bindkey", &CmdBindKey},
    {"setf", &CmdSetFloat},
    {"class", &CmdClass},
    {"struct", &CmdStruct},
    {"func", &CmdFunc},
    {"find", &CmdFind},
    {"mem", &CmdMem},
    {"name", &CmdName},
    {"prop", &CmdProp},
    {"call", &CmdCall},
    {"ctw", &CmdComponentToWorld},
    {"structof", &CmdStructOf},
    {"classof", &CmdClassOf},
    {"arr", &CmdArray},
    {"markpoint", &CmdMarkPoint},
    {"aim", &CmdAim},
    {"scriptrefs", &CmdScriptRefs},
    {"wtree", &CmdWidgetTree},
    {"giveweapon", &CmdGiveWeapon},
    {"shotlog", &CmdShotLog},
    {"adsmode", &CmdAdsMode},
    {"inject", &CmdInject},
    {"report", &CmdReport},
    {"tracking", &CmdTracking},
    {"petrace", &CmdProcessEventTrace},
    {"funcs", &CmdFuncs},
    {"invokep", &CmdInvokeWithParams},
    {"setb", &CmdSetBool},
    {"invoke", &CmdInvoke},
    {"actors", &CmdActors},
    {"tp", &CmdTeleport},
    {"face", &CmdFace},
    {"loc", &CmdLocation},
    {"getb", &CmdGetBool},
    {"boolmask", &CmdBoolMask},
};

void Run(const std::string& line, std::uintptr_t controller) {
    g_controller = controller;
    std::istringstream in(line);
    std::string verb;
    in >> verb;
    std::string rest;
    std::getline(in, rest);
    if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);

    Log::Line("dev: > %s", line.c_str());
    for (const Verb& v : kVerbs) {
        if (verb != v.Name) continue;
        v.Run(rest, controller);
        return;
    }
    Log::Line("dev: unknown verb '%s'", verb.c_str());
}

std::uintptr_t FindLocalController() {
    std::uintptr_t found = 0;
    ue::ForEachUObject([&](std::uintptr_t obj) {
        const std::string cls = ue::ClassName(obj);
        if (!ue::ContainsCI(cls, "PlayerController")) return false;
        if (ue::ObjectName(obj).rfind("Default__", 0) == 0) return false;
        const std::uintptr_t outer = ue::OuterObject(obj);
        if (!outer || ue::ClassName(outer) != "Level") return false;
        found = obj;
        return true;
    });
    return found;
}

std::uint64_t g_lastViewPollMs = 0;

void TickFromProcessEvent() {
    // Its own stamp, not g_lastViewPollMs. That one is only written on the
    // view-hook path, and this tick exists precisely for the states where the
    // view hook is quiet - so the gate never closed, and every single
    // ProcessEvent ran a full UObject table walk looking for a controller that
    // is not there yet. UE dispatches through ProcessEvent many times a frame.
    static std::uint64_t s_lastTickMs = 0;
    const std::uint64_t now = GetTickCount64();
    if (now - s_lastTickMs < 1000) return;
    s_lastTickMs = now;
    if (now - g_lastViewPollMs < 1000) return;
    Poll(0);
}

}  // namespace

void SetEnabled(bool enabled, const std::string& exeDir) {
    g_enabled = enabled && !exeDir.empty();
    g_path = exeDir + "\\HeadTracking.devcmd";
    if (g_enabled) Log::Line("dev: command channel enabled, polling %s", g_path.c_str());
}

void ArmFrontEndPolling() {
    if (!g_enabled) return;
    process_event_hook::Install();
    process_event_hook::SetTick(&TickFromProcessEvent);
}

void Poll(std::uintptr_t controller) {
    if (!g_enabled) return;
    if (controller) g_lastViewPollMs = GetTickCount64();
    else controller = g_controller ? g_controller : FindLocalController();
    if (!g_pendingProjectiles.empty()) {
        const std::vector<std::uintptr_t> pending = std::move(g_pendingProjectiles);
        g_pendingProjectiles.clear();
        const view_hook::AimSample a = view_hook::LastAim();
        for (std::uintptr_t projectile : pending) {
            ue4::FVector loc{};
            if (!ActorLocation(projectile, loc)) continue;
            Log::Line("shot: spawned %s at (%.2f,%.2f,%.2f) | aim origin (%.2f,%.2f,%.2f) dir (%.5f,%.5f,%.5f)",
                      ue::ClassName(projectile).c_str(), loc.X, loc.Y, loc.Z, a.MuzzleX, a.MuzzleY, a.MuzzleZ,
                      a.DirX, a.DirY, a.DirZ);
        }
    }
    if (g_peUntil && GetTickCount64() > g_peUntil) {
        g_peUntil = 0;
        process_event_hook::SetObserver(nullptr);
        std::lock_guard<std::mutex> lk(g_peMutex);
        for (const auto& kv : g_peSeen)
            Log::Line("dev: petrace %6d x %s::%s", kv.second, ue::OuterName(kv.first).c_str(),
                      ue::ObjectName(kv.first).c_str());
    }
    const std::uint64_t now = GetTickCount64();
    if (now - g_lastPollMs < 250) return;
    g_lastPollMs = now;

    std::vector<std::string> lines;
    {
        std::ifstream f(g_path);
        if (!f) return;
        std::string l;
        while (std::getline(f, l)) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            if (!l.empty()) lines.push_back(l);
        }
    }
    DeleteFileA(g_path.c_str());
    for (const std::string& l : lines) Run(l, controller);
}

bool ExecuteConsoleCommand(std::uintptr_t worldContext, const std::wstring& command) {
    static std::uintptr_t fn = 0, cdo = 0;
    static std::size_t size = 0, ctxOff = 0, cmdOff = 0, playerOff = 0;
    if (!ue_vm::Ready()) return false;
    if (!fn) {
        cdo = ue::FindLiveObject("KismetSystemLibrary", "Default__KismetSystemLibrary", nullptr);
        const std::uintptr_t f = ue::FindLiveObject("Function", "ExecuteConsoleCommand", "KismetSystemLibrary");
        std::vector<ue_reflect::FieldInfo> p;
        if (!cdo || !f ||
            !ue_reflect::ResolveAll("ExecuteConsoleCommand", f,
                                    {"WorldContextObject", "Command", "SpecificPlayer"}, p))
            return false;
        size = ue_reflect::StructSize(f);
        // Every slot at the width written into it, not just the string: a
        // pointer written into a slot the engine reports as narrower runs the
        // tail of that write off the end of `buf`.
        const std::size_t ptrBytes = sizeof(std::uintptr_t);
        if (size == 0 || size > 256 || !ue_reflect::FieldFits(p[0], ptrBytes, size) ||
            !ue_reflect::FieldFits(p[1], sizeof(FStringHeader), size) ||
            !ue_reflect::FieldFits(p[2], ptrBytes, size))
            return false;
        ctxOff = p[0].Offset; cmdOff = p[1].Offset; playerOff = p[2].Offset;
        fn = f;
    }
    alignas(16) unsigned char buf[256];
    std::memset(buf, 0, size);
    std::memcpy(buf + ctxOff, &worldContext, sizeof(worldContext));
    const FStringHeader str{command.c_str(), static_cast<std::int32_t>(command.size() + 1),
                            static_cast<std::int32_t>(command.size() + 1)};
    std::memcpy(buf + cmdOff, &str, sizeof(str));
    std::memcpy(buf + playerOff, &worldContext, sizeof(worldContext));
    return ue_vm::Dispatch(reinterpret_cast<void*>(cdo), reinterpret_cast<void*>(fn), buf);
}

}  // namespace t2_ht::dev_console
