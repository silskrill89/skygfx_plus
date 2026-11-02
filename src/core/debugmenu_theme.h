#pragma once
// ============================================================
// debugmenu_theme.h - Aero/Bento visual foundation for the SkyGFX
// ImGui debug menu.
//
// Design language: Vista Aero glass warmed up for 2026 - dark glass
// panels, 1px cool borders, restrained 8px corners, and GTA San
// Andreas taxi-yellow (#F2C21A, straight out of carcols.dat) as the
// only saturated accent. Everything visual lives here so the render
// pass can grow (blur, textures, animation) without touching the
// settings logic in debugmenu_ui.cpp.
//
// No textures, no shaders, no D3D9 calls: ImGui draw lists only, so
// the existing D3DSBT_ALL state wrapper stays in charge.
// ============================================================

#include "imgui.h"

// Semantic palette. Values are 0xRRGGBB (alpha is applied by the
// helpers below), so they can be pasted into an INI/theme file later
// without unpacking ImVec4s.
struct DmPalette
{
	ImU32 bgDeep;		// app/window backdrop
	ImU32 bgPanel;		// child panels, popups
	ImU32 bgRaised;		// buttons, group bands, title bar
	ImU32 bgHover;
	ImU32 bgActive;
	ImU32 border;		// normal 1px chrome
	ImU32 borderLight;	// table outer borders, focused chrome
	ImU32 separator;	// hairlines inside panels
	ImU32 textPrimary;
	ImU32 textSecondary;
	ImU32 textDisabled;
	ImU32 accent;		// GTA taxi yellow
	ImU32 accentHover;
	ImU32 accentDim;
	ImU32 ok;
	ImU32 warn;
	ImU32 error;
	ImU32 info;
	ImU32 glassSheen;	// white; used at low alpha for the top sheen
	ImU32 shadow;		// black; used at low alpha under bands
};

// Component sizes. Corner radii are deliberately restrained: Aero
// used 6-8px, not pills, and the 2026 update keeps crisp 1px edges.
struct DmMetrics
{
	float  windowRounding;
	float  childRounding;
	float  frameRounding;
	float  grabRounding;
	float  tabRounding;
	float  headerRounding;		// glass header/group band corners
	float  windowBorderSize;
	float  frameBorderSize;
	float  scrollbarSize;
	ImVec2 itemSpacing;
	ImVec2 windowPadding;
	ImVec2 framePadding;
	ImVec2 headerPadding;		// x: label inset, y: vertical band inset
};

extern const DmPalette g_dmPalette;
extern const DmMetrics g_dmMetrics;

// Palette helpers (palette values are RGB-only).
ImVec4 DmCol(ImU32 rgb, float alpha = 1.0f);
ImU32  DmColU32(ImU32 rgb, float alpha = 1.0f);
ImU32  DmMix(ImU32 a, ImU32 b, float t);	// lerp in sRGB, t=0 -> a

// Installs the full style (metrics + ImGuiCol_* table). Call once,
// after ImGui::CreateContext(), before the first NewFrame.
void DebugMenu_ApplyTheme(void);

// ------------------------------------------------------------
// D3D9 device lifetime. ImGui's DX9 backend keeps its vertex/index
// buffers in D3DPOOL_DEFAULT and its font atlas in a device texture,
// so IDirect3DDevice9::Reset invalidates all of them. depthhook.cpp
// calls these around Reset. Every entry point is a no-op until
// ImGui_ImplDX9_Init() has succeeded.
// ------------------------------------------------------------
bool UI_ImGuiIsInitialized(void);
void UI_ImGuiSetInitialized(bool initialized);	// after a successful ImGui_ImplDX9_Init
void UI_ImGuiInvalidateDeviceObjects(void);	// before Reset
void UI_ImGuiCreateDeviceObjects(void);		// after Reset; rebuilds + re-applies style

// ------------------------------------------------------------
// Custom draw helpers. All of them draw into the current window's
// ImDrawList and never touch D3D state.
// ------------------------------------------------------------

// Dark glass band: gradient sheen, 1px top highlight, 1px bottom
// border, rounded corners, and a 3px taxi-yellow tab on the left.
// Advances the cursor by `size` (y defaults to text height + 10,
// x defaults to the full content width).
void DrawGlassHeader(const char* label, ImVec2 size = ImVec2(0.0f, 0.0f));

// Section heading that behaves like CollapsingHeader (state kept in
// the window's ImGuiStorage, default open). Returns the open state.
// Built on DrawGlassHeader plus a right-side chevron and a hover ring.
bool DrawGlassCollapsingHeader(const char* label, bool defaultOpen = true);

// 1px rule with a 42px taxi-yellow segment at the left; adds the
// same vertical rhythm as ImGui::Separator().
void DrawAccentSeparator(void);

// Small state pill. rightScreenX > 0 aligns the pill's right edge to
// that screen coordinate; pass -1 to place it inline after the last
// item. Draws only, so row layout is unchanged.
enum DmBadgeKind
{
	DM_BADGE_OVERRIDDEN,
	DM_BADGE_INACTIVE,
	DM_BADGE_RESTART,
	DM_BADGE_NEW,
	DM_BADGE_OK,
	DM_BADGE_WARN,
	DM_BADGE_ERROR
};

void DrawStatusBadge(DmBadgeKind kind, float rightScreenX = -1.0f);
ImVec2 StatusBadgeSize(DmBadgeKind kind);	// for callers that reserve space

// Small "MODE" pill for a settings row whose INI key is set by the active
// mode profile. Drawn only (no item submitted), so the previous item's rect
// still anchors a following status badge. rightScreenX > 0 aligns the pill's
// right edge; pass -1 to place it inline after the last item. tip may be
// NULL. The text is baked at 10px from the current font - no extra atlas.
ImVec2 ModeOwnedBadgeSize(void);
void DrawModeOwnedBadge(const char* tip, float rightScreenX = -1.0f);
