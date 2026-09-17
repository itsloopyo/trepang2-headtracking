// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "reticle.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include <windows.h>

#include "logging.h"
#include "ue4_types.h"
#include "ue_call.h"
#include "ue_reflect.h"
#include "ue_vm.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace t2_ht::reticle {

namespace {

namespace ue = ::cameraunlock::unreal;

// The ADS marker is a second instance of the game's crosshair widget.
constexpr const char* kCrosshairClass = "WBCrosshair_C";
constexpr int kMaxOuterDepth = 8;
constexpr std::size_t kInternalIndexOffset = 0x0c;

ue_vm::ResolveRetry g_resolveRetry;
bool g_resolved = false;
std::uintptr_t g_layoutLib = 0;
ue_call::Function g_setTranslation;   // Widget::SetRenderTranslation
ue_call::Function g_viewportScale;    // WidgetLayoutLibrary::GetViewportScale
ue_call::Function g_viewportSize;     // WidgetLayoutLibrary::GetViewportSize

// PlayerBP_C.PlayerHud, resolved against the pawn class it was first read on.
std::uintptr_t g_pawnClass = 0;
ue_reflect::FieldInfo g_hudField;
// The pawn class whose PlayerHud lookup failed, so the retry gate applies to
// repeats of that class only and never to a class being seen for the first time.
// The flag is separate because ue_call::ClassOf answers 0 on a failed read, and
// a bare 0 sentinel would swallow the one log line that case needs to produce.
std::uintptr_t g_hudFailedClass = 0;
bool g_hudResolveFailed = false;
ue_vm::ResolveRetry g_hudRetry;

// The HUD panels that are moved, each holding something the game draws on the
// aim point. CrosshairBox carries the crosshair, the stealth dot and the kill
// confirmation. The tactical visor replaces that whole mode with its own
// crosshair (Scanner_CrosshairPanel) and the label of the target under it
// (Scanner_Overlay). The rest are the game's other marks on the aim point: the
// prompts for whatever is under the crosshair - the weapon pickup with its two
// ammo counts and hold-to-take bar, the interaction prompt and its hold bar,
// the grenade pickup - then the ability keys with their charge rings, and the
// out-of-ammo text. All of them are laid out on the middle of the screen or
// just below it, which is the crosshair only while the head is centred.
//
// Their siblings under the same parent are not moved: the stamina bar, the
// health and ammo corner and the damage flash are status readouts the game
// pins to an edge of the screen, and they belong where it put them.

// What a readback found. Offscreen and Unpainted are their own answers rather
// than misplacements: the visor's panels sit behind an inactive mode for most of
// a session and the prompts are hidden until the player looks at something, and
// Slate leaves a widget it is not drawing with the cached geometry it last had,
// so calling either "not where it was put" cries wolf in every ordinary session.
enum class Paint { Unknown, Offscreen, Unpainted, Placed, Misplaced };

struct Panel {
    const char* Name;
    const char* Class;
    std::uintptr_t Widget = 0;
    std::int32_t Index = -1;
    float LastX = 0.0f, LastY = 0.0f;
    // Where the game draws this panel with no offset applied, in viewport
    // pixels, learned from a painted frame. Only CrosshairBox is laid out on
    // the middle of the screen; the rest sit at their own offsets from it.
    bool HaveLayout = false;
    float LayoutX = 0.0f, LayoutY = 0.0f;
    // What the last readback said, so the next one only writes a line when the
    // answer has changed.
    Paint LastPaint = Paint::Unknown;
    bool Drawn = false;
};
Panel g_panels[] = {
    {"CrosshairBox", "CanvasPanel"},
    {"Scanner_CrosshairPanel", "CanvasPanel"},
    {"Scanner_Overlay", "VerticalBox"},
    {"WeaponSwapPanel", "CanvasPanel"},
    {"UI_Interaction", "Overlay"},
    {"GrenadeSwapCanvasPanel", "CanvasPanel"},
    {"AbilitiesPanelNew", "CanvasPanel"},
    {"NoAmmoPanel", "CanvasPanel"},
};

std::uintptr_t g_hud = 0;
ue_vm::ResolveRetry g_findRetry;
// Whether the panels have been moved at least once, so the first move writes
// the viewport and DPI it was made against and the rest stay silent.
bool g_bound = false;

// ESlateVisibility
constexpr std::uint8_t kCollapsed = 1;
constexpr std::uint8_t kHitTestInvisible = 3;

// Layout box the marker instance is given, in viewport units. The crosshair's
// lines are anchored to the middle of its root canvas, so the box only has to
// be large enough to hold them; the alignment centres it on the position.
constexpr float kMarkerBox = 128.0f;

std::uintptr_t g_widgetLib = 0;
std::uintptr_t g_crosshairClass = 0;
ue_call::Function g_create;           // WidgetBlueprintLibrary::Create
ue_call::Function g_addToViewport;    // UserWidget::AddToViewport
ue_call::Function g_setPosInViewport; // UserWidget::SetPositionInViewport
ue_call::Function g_setDesiredSize;   // UserWidget::SetDesiredSizeInViewport
ue_call::Function g_setAlignment;     // UserWidget::SetAlignmentInViewport
ue_call::Function g_setVisibility;    // Widget::SetVisibility

bool Resolve() {
    if (g_resolved) return true;
    if (!g_resolveRetry.Due() || !ue_vm::Ready()) return false;
    g_layoutLib = ue_call::DefaultObject("WidgetLayoutLibrary");
    g_widgetLib = ue_call::DefaultObject("WidgetBlueprintLibrary");
    g_crosshairClass = ue::FindLiveObject("WidgetBlueprintGeneratedClass", kCrosshairClass, nullptr);
    const std::size_t ptr = sizeof(std::uintptr_t);
    const std::size_t v2 = sizeof(ue4::FVector2D);
    if (!g_layoutLib || !g_widgetLib || !g_crosshairClass ||
        !g_setTranslation.Resolve("Widget", "SetRenderTranslation", {{"Translation", v2}}) ||
        !g_viewportScale.Resolve("WidgetLayoutLibrary", "GetViewportScale",
                                 {{"WorldContextObject", ptr}, {"ReturnValue", sizeof(float)}}) ||
        !g_viewportSize.Resolve("WidgetLayoutLibrary", "GetViewportSize",
                                {{"WorldContextObject", ptr}, {"ReturnValue", v2}}) ||
        !g_create.Resolve("WidgetBlueprintLibrary", "Create",
                          {{"WorldContextObject", ptr}, {"WidgetType", ptr}, {"OwningPlayer", ptr},
                           {"ReturnValue", ptr}}) ||
        !g_addToViewport.Resolve("UserWidget", "AddToViewport", {{"ZOrder", sizeof(std::int32_t)}}) ||
        !g_setPosInViewport.Resolve("UserWidget", "SetPositionInViewport",
                                    {{"Position", v2}, {"bRemoveDPIScale", 1}}) ||
        !g_setDesiredSize.Resolve("UserWidget", "SetDesiredSizeInViewport", {{"Size", v2}}) ||
        !g_setAlignment.Resolve("UserWidget", "SetAlignmentInViewport", {{"Alignment", v2}}) ||
        !g_setVisibility.Resolve("Widget", "SetVisibility", {{"InVisibility", 1}}))
        return false;
    g_resolved = true;
    return true;
}

// ---- readback: where Slate last drew the crosshair ----------------------
// The move is a request; the paint is the fact. Once a second the bound
// widget's render translation and its last painted geometry are read back and
// compared with the position that was asked for, so "the crosshair is not where
// the mod put it" is a line in the log rather than a guess.
//
// A panel is compared against its OWN drawn position with no offset applied,
// learned from a painted frame, rather than against the middle of the screen.
// Only CrosshairBox is laid out there: the visor's target label was measured at
// (830,425) in a 1280x720 viewport, and the pickup and interaction prompts sit
// below the crosshair, so predicting any of them from the centre reports every
// painted frame as misplaced.
//
// Only a CHANGE in that comparison is written. Logging every sample put three
// lines a second into the log for as long as the player was in gameplay - 76%
// of a nine-minute session's log, all of it repeating that the crosshair was
// exactly where it was asked to be - which buries the build match, the link and
// the gate transitions a report is read for.
constexpr std::uint64_t kReadbackMs = 1000;
// How still the offset has to be between two frames for the readback to compare
// anything at all. See ReadBackPanels.
constexpr float kSettledPx = 2.0f;
// What counts as drawn where it was put. The geometry read back is a frame
// behind the translation read with it, and the layout each panel is measured
// against was itself learned from such a frame, so a few pixels of lag is the
// normal state even with a settled pose. A panel that has stopped following is
// out by the whole offset, which is hundreds.
constexpr float kDrawTolerancePx = 8.0f;
ue_vm::ResolveRetry g_readbackRetry;
bool g_readbackResolved = false;
std::uintptr_t g_slateLib = 0;
ue_call::Function g_cachedGeometry;   // Widget::GetCachedGeometry
ue_call::Function g_localToViewport;  // SlateBlueprintLibrary::LocalToViewport
ue_call::Function g_localSize;        // SlateBlueprintLibrary::GetLocalSize
ue_call::Function g_isVisible;        // Widget::IsVisible
std::size_t g_geometrySize = 0;
std::size_t g_renderTransformOffset = 0;
std::uint64_t g_lastReadbackMs = 0;
// The viewport and DPI every panel's layout was learned against.
ue4::FVector2D g_layoutViewport{0.0f, 0.0f};
float g_layoutDpi = 0.0f;
// The offset the frame before this one asked for, in viewport pixels.
float g_prevPixelX = 0.0f, g_prevPixelY = 0.0f;

bool ResolveReadback() {
    if (g_readbackResolved) return true;
    if (!g_readbackRetry.Due()) return false;
    const std::uintptr_t geometry = ue::FindLiveObject("ScriptStruct", "Geometry", nullptr);
    const std::uintptr_t widgetClass = ue::FindLiveObject("Class", "Widget", nullptr);
    g_slateLib = ue_call::DefaultObject("SlateBlueprintLibrary");
    ue_reflect::FieldInfo transform;
    if (!geometry || !widgetClass || !g_slateLib ||
        !ue_reflect::FindPropertyInChain(widgetClass, "RenderTransform", transform))
        return false;
    g_geometrySize = ue_reflect::StructSize(geometry);
    // A zero size would satisfy every FieldFits below, and the readback would
    // then hand the engine a geometry of no bytes and report "has not been
    // drawn" for the rest of the session with nothing saying why.
    if (g_geometrySize == 0 || g_geometrySize > ue_call::Function::kMaxFrame) return false;
    const std::size_t v2 = sizeof(ue4::FVector2D);
    if (!g_cachedGeometry.Resolve("Widget", "GetCachedGeometry", {{"ReturnValue", g_geometrySize}}) ||
        !g_localToViewport.Resolve("SlateBlueprintLibrary", "LocalToViewport",
                                   {{"WorldContextObject", sizeof(std::uintptr_t)}, {"Geometry", g_geometrySize},
                                    {"LocalCoordinate", v2}, {"PixelPosition", v2}, {"ViewportPosition", v2}}) ||
        !g_localSize.Resolve("SlateBlueprintLibrary", "GetLocalSize",
                             {{"Geometry", g_geometrySize}, {"ReturnValue", v2}}) ||
        !g_isVisible.Resolve("Widget", "IsVisible", {{"ReturnValue", 1}}))
        return false;
    g_renderTransformOffset = transform.Offset;
    g_readbackResolved = true;
    return true;
}

struct Drawn {
    bool Valid = false;
    bool Visible = false;
    ue4::FVector2D Translation{0.0f, 0.0f};
    ue4::FVector2D Centre{0.0f, 0.0f};   // viewport pixels
    ue4::FVector2D Size{0.0f, 0.0f};     // local units
};

Drawn ReadDrawn(std::uintptr_t controller, std::uintptr_t widget) {
    Drawn d;
    if (!widget || !ResolveReadback()) return d;
    ue::SafeReadFloat(widget + g_renderTransformOffset, d.Translation.X);
    ue::SafeReadFloat(widget + g_renderTransformOffset + 4, d.Translation.Y);
    ue_call::Frame vis(g_isVisible);
    if (vis.Call(widget)) d.Visible = (vis.Get<std::uint8_t>(0) & 1u) != 0;
    ue_call::Frame geo(g_cachedGeometry);
    if (!geo.Call(widget)) return d;
    ue_call::Frame size(g_localSize);
    std::memcpy(size.Data() + g_localSize.Offset(0), geo.At(0), g_geometrySize);
    if (!size.Call(g_slateLib)) return d;
    d.Size = size.Get<ue4::FVector2D>(1);
    ue_call::Frame toViewport(g_localToViewport);
    toViewport.Set(0, controller);
    std::memcpy(toViewport.Data() + g_localToViewport.Offset(1), geo.At(0), g_geometrySize);
    toViewport.Set(2, ue4::FVector2D{d.Size.X * 0.5f, d.Size.Y * 0.5f});
    if (!toViewport.Call(g_slateLib)) return d;
    d.Centre = toViewport.Get<ue4::FVector2D>(3);
    d.Valid = true;
    return d;
}

// Drop every binding, keeping only what identifies each panel. The next Bind()
// goes looking again.
void ResetPanels() {
    for (Panel& panel : g_panels) panel = Panel{panel.Name, panel.Class};
}

bool OuterChainContains(std::uintptr_t obj, std::uintptr_t ancestor) {
    std::uintptr_t cur = ue::OuterObject(obj);
    for (int depth = 0; depth < kMaxOuterDepth && cur; ++depth) {
        if (cur == ancestor) return true;
        cur = ue::OuterObject(cur);
    }
    return false;
}

// A widget the engine has destroyed leaves its object slot to be reused by
// something else, so the index has to still hold the same object.
bool StillAlive(std::uintptr_t obj, std::int32_t index) {
    const std::uintptr_t item = ue_call::ObjectItem(index);
    std::uintptr_t live = 0;
    return obj != 0 && item != 0 && ue::SafeReadPtr(item, live) && live == obj;
}

std::uintptr_t CurrentHud(std::uintptr_t pawn) {
    const std::uintptr_t cls = ue_call::ClassOf(pawn);
    if (cls != g_pawnClass) {
        // A class that already failed is retried on the 250ms gate; a class not
        // seen before is resolved on the spot, so a respawn or a level load
        // binds on the frame it happens.
        //
        // The failure must not be cached the way a success is. Caching it
        // disabled crosshair compensation for the rest of the session off one
        // bad read, with Publish returning here and nothing in the log - the
        // player reports a crosshair that does not move and there is no line to
        // point at. Retrying it every frame would put a property chain walk in
        // the render hook instead.
        if (g_hudResolveFailed && cls == g_hudFailedClass && !g_hudRetry.Due()) return 0;
        // Resolved into a local and committed only on success. Writing straight
        // into g_hudField would destroy the working offset for the class that
        // IS cached the moment one read returns a different class - and
        // ue_call::ClassOf answers 0 on a failed read, so that happens on any
        // bad frame. The next frame then sees the real class again, matches
        // g_pawnClass, skips this block entirely and never rebuilds the field:
        // the crosshair freezes at its last offset for the rest of the session.
        ue_reflect::FieldInfo field;
        if (!ue_reflect::FindPropertyInChain(cls, "PlayerHud", field) ||
            field.Size != sizeof(std::uintptr_t)) {
            if (!g_hudResolveFailed || g_hudFailedClass != cls) {
                g_hudResolveFailed = true;
                g_hudFailedClass = cls;
                Log::Line("reticle: no PlayerHud property on %s - retrying", ue::ClassName(cls).c_str());
            }
            return 0;
        }
        g_hudField = field;
        g_pawnClass = cls;
        g_hudResolveFailed = false;
        g_hudFailedClass = 0;
    }
    std::uintptr_t hud = 0;
    if (g_hudField.Size != sizeof(std::uintptr_t) || !ue::SafeReadPtr(pawn + g_hudField.Offset, hud)) return 0;
    return hud;
}

bool PanelsAlive() {
    for (const Panel& panel : g_panels)
        if (!StillAlive(panel.Widget, panel.Index)) return false;
    return true;
}

// The panels inside THIS pawn's HUD. The front end and a previous level can
// leave other HUD instances in the object table, and moving one of those
// changes nothing on screen while looking exactly like success.
bool Bind(std::uintptr_t pawn) {
    const std::uintptr_t hud = CurrentHud(pawn);
    if (!hud) return false;
    if (hud == g_hud && PanelsAlive()) return true;
    g_hud = hud;
    ResetPanels();
    if (!g_findRetry.Due()) return false;
    ue::ForEachUObject([&](std::uintptr_t obj) {
        const std::string cls = ue::ClassName(obj);
        if (cls != "CanvasPanel" && cls != "VerticalBox" && cls != "Overlay") return false;
        const std::string name = ue::ObjectName(obj);
        for (Panel& panel : g_panels) {
            std::uint32_t index = 0;
            if (panel.Widget || name != panel.Name || cls != panel.Class || !OuterChainContains(obj, hud) ||
                !ue::SafeReadU32(obj + kInternalIndexOffset, index))
                continue;
            panel.Widget = obj;
            panel.Index = static_cast<std::int32_t>(index);
        }
        return false;
    });
    for (Panel& panel : g_panels) {
        if (panel.Widget) continue;
        Log::Line("reticle: %s not found in %s (0x%llx)", panel.Name, ue::ClassName(hud).c_str(),
                  static_cast<unsigned long long>(hud));
        ResetPanels();
        return false;
    }
    for (const Panel& panel : g_panels)
        Log::Line("reticle: bound %s (0x%llx) in %s", panel.Name, static_cast<unsigned long long>(panel.Widget),
                  ue::ClassName(hud).c_str());
    return true;
}

bool Move(float x, float y) {
    for (Panel& panel : g_panels) {
        if (x == panel.LastX && y == panel.LastY) continue;
        ue_call::Frame frame(g_setTranslation);
        frame.Set(0, ue4::FVector2D{x, y});
        if (!frame.Call(panel.Widget)) {
            ResetPanels();
            return false;
        }
        panel.LastX = x;
        panel.LastY = y;
    }
    return true;
}

// ---- the ADS marker instance ----------------------------------------------
std::uintptr_t g_marker = 0;
std::int32_t g_markerIndex = -1;
std::uintptr_t g_markerController = 0;
bool g_markerShown = false;
float g_markerX = -1.0f, g_markerY = -1.0f;

void SetMarkerShown(bool shown) {
    if (shown == g_markerShown) return;
    ue_call::Frame vis(g_setVisibility);
    vis.Set(0, shown ? kHitTestInvisible : kCollapsed);
    // Only on a dispatch that landed. Committing regardless desyncs the flag
    // from the widget, and every later call with the same value is then skipped
    // by the guard above - so the marker never reappears, or never hides.
    if (vis.Call(g_marker)) g_markerShown = shown;
}

// Hide before dropping the handle. A widget let go of while visible stays in
// the viewport at its last position with nothing owning it, and the next frame
// adds a SECOND crosshair instance - an abandoned mark sitting at a stale aim
// point for the rest of the level.
//
// Only while it is still live: a dead object is already out of the viewport,
// and dispatching into it spends a ProcessEvent round trip on the render path
// to have the fault absorbed.
void DropMarker() {
    if (g_marker && StillAlive(g_marker, g_markerIndex)) SetMarkerShown(false);
    g_marker = 0;
    g_markerIndex = -1;
}

bool BuildMarker(std::uintptr_t controller) {
    ue_call::Frame create(g_create);
    create.Set(0, controller);
    create.Set(1, g_crosshairClass);
    create.Set(2, controller);
    if (!create.Call(g_widgetLib)) return false;
    const auto widget = create.Get<std::uintptr_t>(3);
    std::uint32_t index = 0;
    if (!widget || !ue::SafeReadU32(widget + kInternalIndexOffset, index)) return false;

    ue_call::Frame size(g_setDesiredSize);
    size.Set(0, ue4::FVector2D{kMarkerBox, kMarkerBox});
    size.Call(widget);
    ue_call::Frame align(g_setAlignment);
    align.Set(0, ue4::FVector2D{0.5f, 0.5f});
    align.Call(widget);
    ue_call::Frame vis(g_setVisibility);
    vis.Set(0, kCollapsed);
    vis.Call(widget);
    ue_call::Frame add(g_addToViewport);
    add.Set(0, std::int32_t{10000});
    if (!add.Call(widget)) return false;

    g_marker = widget;
    g_markerIndex = static_cast<std::int32_t>(index);
    g_markerController = controller;
    g_markerShown = false;
    g_markerX = g_markerY = -1.0f;
    Log::Line("reticle: ADS marker built (0x%llx)", static_cast<unsigned long long>(widget));
    return true;
}

// The viewport size and DPI scale, or false when they do not read.
bool Viewport(std::uintptr_t controller, ue4::FVector2D& size, float& dpi) {
    ue_call::Frame sizeFrame(g_viewportSize);
    sizeFrame.Set(0, controller);
    ue_call::Frame scale(g_viewportScale);
    scale.Set(0, controller);
    if (!sizeFrame.Call(g_layoutLib) || !scale.Call(g_layoutLib)) return false;
    size = sizeFrame.Get<ue4::FVector2D>(1);
    dpi = scale.Get<float>(1);
    return size.X > 0.0f && size.Y > 0.0f && dpi > 0.0f;
}

// What the last readback said about one panel, against the centre the move
// asked for. Only a CHANGE is written: see kReadbackMs.
void LogPanelPaint(Panel& panel, const Drawn& d, float askedX, float askedY, float pixelX,
                   float pixelY, const ue4::FVector2D& viewport, float dpi) {
    const bool painted = d.Valid && d.Visible && d.Size.X > 0.0f && d.Size.Y > 0.0f;
    // The first painted frame is what the panel's layout is read from, so it
    // has nothing to be compared against and says only that it has been drawn.
    if (painted && !panel.HaveLayout) {
        panel.LayoutX = d.Centre.X - pixelX;
        panel.LayoutY = d.Centre.Y - pixelY;
        panel.HaveLayout = true;
    }
    const float expectedX = panel.LayoutX + pixelX;
    const float expectedY = panel.LayoutY + pixelY;
    const Paint paint =
        !d.Visible ? Paint::Offscreen
        : !painted ? Paint::Unpainted
        : std::fabs(d.Centre.X - expectedX) <= kDrawTolerancePx &&
          std::fabs(d.Centre.Y - expectedY) <= kDrawTolerancePx ? Paint::Placed
                                                                : Paint::Misplaced;
    if (paint == panel.LastPaint && d.Visible == panel.Drawn) return;
    panel.LastPaint = paint;
    panel.Drawn = d.Visible;
    // A panel the game has never drawn has no layout to expect it at, and
    // printing the bare offset there reads as a screen position it was due at.
    char expected[32] = "not measured yet";
    if (panel.HaveLayout)
        std::snprintf(expected, sizeof(expected), "%.0f,%.0f", expectedX, expectedY);
    Log::Line("reticle: %s %s - asked translation (%.1f,%.1f) -> expected centre (%s) px | "
              "translation (%.1f,%.1f) visible=%d drawn centre (%.0f,%.0f) px size (%.1f,%.1f) valid=%d | "
              "viewport %.0fx%.0f dpi %.3f",
              panel.Name,
              paint == Paint::Offscreen ? "is not on screen"
                  : paint == Paint::Unpainted ? "has not been drawn"
                      : paint == Paint::Placed ? "is drawn where it was put"
                                               : "is NOT drawn where it was put",
              askedX, askedY, expected,
              d.Translation.X, d.Translation.Y, d.Visible ? 1 : 0, d.Centre.X, d.Centre.Y, d.Size.X,
              d.Size.Y, d.Valid ? 1 : 0, viewport.X, viewport.Y, dpi);
}

// Once a second, where Slate actually drew each panel against where it was put.
// The move is a request; the paint is the fact.
//
// `settled` is whether this frame's offset is within a pixel or two of the one
// before it. The geometry read back is the frame BEFORE the translation read
// alongside it, so a head in motion moves the crosshair between the two and
// every panel reads as one that has not followed - the old check wrote a
// contradictory line roughly every other second for as long as the player was
// looking around.
void ReadBackPanels(std::uintptr_t controller, bool settled, float askedX, float askedY,
                    float pixelX, float pixelY, const ue4::FVector2D& viewport, float dpi) {
    const std::uint64_t now = GetTickCount64();
    if (now - g_lastReadbackMs < kReadbackMs || !settled) return;
    g_lastReadbackMs = now;

    // A resized viewport or a changed DPI curve moves every panel's layout, and
    // the offset that was learned against the old one is no longer where the
    // game draws it.
    if (viewport.X != g_layoutViewport.X || viewport.Y != g_layoutViewport.Y || dpi != g_layoutDpi) {
        g_layoutViewport = viewport;
        g_layoutDpi = dpi;
        for (Panel& panel : g_panels) panel.HaveLayout = false;
    }
    for (Panel& panel : g_panels)
        LogPanelPaint(panel, ReadDrawn(controller, panel.Widget), askedX, askedY, pixelX, pixelY,
                      viewport, dpi);
}

}  // namespace

bool PublishMarker(std::uintptr_t controller, bool visible, float ndcX, float ndcY) {
    if (g_marker && (controller != g_markerController || !StillAlive(g_marker, g_markerIndex))) {
        DropMarker();
    }
    if (!visible) {
        if (g_marker) SetMarkerShown(false);
        return false;
    }
    if (!controller || !Resolve()) return false;
    if (!g_marker && !BuildMarker(controller)) return false;

    ue4::FVector2D viewport{};
    float dpi = 1.0f;
    if (!Viewport(controller, viewport, dpi)) {
        SetMarkerShown(false);
        return false;
    }
    // Absolute viewport pixels; SetPositionInViewport removes the DPI scale itself.
    const float x = std::round((ndcX + 1.0f) * 0.5f * viewport.X);
    const float y = std::round((1.0f - ndcY) * 0.5f * viewport.Y);
    if (x != g_markerX || y != g_markerY) {
        ue_call::Frame pos(g_setPosInViewport);
        pos.Set(0, ue4::FVector2D{x, y});
        pos.Set(1, std::uint8_t{1});
        if (!pos.Call(g_marker)) {
            DropMarker();
            return false;
        }
        g_markerX = x;
        g_markerY = y;
    }
    SetMarkerShown(true);
    return true;
}

void Publish(std::uintptr_t controller, std::uintptr_t pawn, bool valid, float ndcX, float ndcY) {
    if (!pawn || !Resolve() || !Bind(pawn)) return;
    if (!valid) {
        Move(0.0f, 0.0f);
        return;
    }

    ue4::FVector2D viewport{};
    float dpi = 1.0f;
    if (!Viewport(controller, viewport, dpi)) {
        Move(0.0f, 0.0f);
        return;
    }
    // Render translation is in the widget's layout units, which the viewport
    // scales by the DPI curve; screen y runs down.
    const float pixelX = std::round(ndcX * viewport.X * 0.5f);
    const float pixelY = std::round(-ndcY * viewport.Y * 0.5f);
    const float tx = pixelX / dpi;
    const float ty = pixelY / dpi;
    if (Move(tx, ty) && !g_bound) {
        g_bound = true;
        Log::Line("reticle: first move (viewport %.0fx%.0f, DPI scale %.3f)", viewport.X, viewport.Y, dpi);
    }

    const bool settled = std::fabs(pixelX - g_prevPixelX) <= kSettledPx &&
                         std::fabs(pixelY - g_prevPixelY) <= kSettledPx;
    g_prevPixelX = pixelX;
    g_prevPixelY = pixelY;
    ReadBackPanels(controller, settled, tx, ty, pixelX, pixelY, viewport, dpi);
}

}  // namespace t2_ht::reticle
