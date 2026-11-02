#include "debugmenu_theme.h"
#include "imgui_impl_dx9.h"
#include <stdint.h>

// skygfx.h's log sink. Declared locally so this visual-foundation TU does not
// pull the whole renderer header just for the reset receipts.
void dbglog(const char *fmt, ...);

// ============================================================
// Palette - "dark glass + taxi yellow".
//
// The neutrals are deliberately cool (blue-grey) while every accent
// is warm, which is what makes the Aero chrome read as glass instead
// of as flat grey panels. Contrast checked against bgPanel/bgDeep:
//   textPrimary   ~13.9:1
//   textSecondary ~ 7.1:1
//   textDisabled  ~ 3.4:1  (metadata only, never the only signal)
//   accent        ~ 9.8:1  (labels, badges, active tab overline)
// ============================================================
const DmPalette g_dmPalette = {
	0x0E1013,	// bgDeep
	0x14171B,	// bgPanel
	0x1B1F24,	// bgRaised
	0x23282E,	// bgHover
	0x2B3138,	// bgActive
	0x343A41,	// border
	0x454C55,	// borderLight
	0x242A30,	// separator
	0xE8EAED,	// textPrimary
	0xA8B0B8,	// textSecondary
	0x6B737C,	// textDisabled
	0xF2C21A,	// accent (GTA taxi yellow)
	0xFFD84D,	// accentHover
	0x8A6F10,	// accentDim
	0x4FAE6A,	// ok
	0xE0A33A,	// warn
	0xD9534F,	// error
	0x4A90D9,	// info
	0xFFFFFF,	// glassSheen
	0x000000	// shadow
};

// Aero geometry: 8px window, 6px child, 5px controls, 1px chrome.
const DmMetrics g_dmMetrics = {
	8.0f,				// windowRounding
	6.0f,				// childRounding
	5.0f,				// frameRounding
	4.0f,				// grabRounding
	5.0f,				// tabRounding
	6.0f,				// headerRounding
	1.0f,				// windowBorderSize
	1.0f,				// frameBorderSize
	12.0f,				// scrollbarSize
	ImVec2(8.0f, 6.0f),	// itemSpacing
	ImVec2(10.0f, 8.0f),// windowPadding
	ImVec2(6.0f, 4.0f),	// framePadding
	ImVec2(12.0f, 5.0f)	// headerPadding
};

// ------------------------------------------------------------
// Colour helpers
// ------------------------------------------------------------
ImVec4
DmCol(ImU32 rgb, float alpha)
{
	ImVec4 v;
	v.x = (float)((rgb >> 16) & 0xFF) / 255.0f;
	v.y = (float)((rgb >> 8) & 0xFF) / 255.0f;
	v.z = (float)(rgb & 0xFF) / 255.0f;
	v.w = alpha;
	return v;
}

ImU32
DmColU32(ImU32 rgb, float alpha)
{
	if(alpha <= 0.0f)
		return 0;
	if(alpha > 1.0f)
		alpha = 1.0f;
	ImU32 a = (ImU32)(alpha * 255.0f + 0.5f);
	return (rgb & 0x00FFFFFF) | (a << 24);
}

ImU32
DmMix(ImU32 a, ImU32 b, float t)
{
	if(t <= 0.0f) return a;
	if(t >= 1.0f) return b;
	ImU32 ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
	ImU32 br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
	ImU32 r = (ImU32)((float)ar + ((float)br - (float)ar) * t + 0.5f);
	ImU32 g = (ImU32)((float)ag + ((float)bg - (float)ag) * t + 0.5f);
	ImU32 bl = (ImU32)((float)ab + ((float)bb - (float)ab) * t + 0.5f);
	return (r << 16) | (g << 8) | bl;
}

// ============================================================
// Theme install - metrics + every ImGuiCol_ the menu touches.
// Called once from EnsureImGuiInit(), before the first frame.
// ============================================================
void
DebugMenu_ApplyTheme(void)
{
	if(ImGui::GetCurrentContext() == NULL)
		return;

	ImGuiStyle &st = ImGui::GetStyle();
	const DmPalette &p = g_dmPalette;
	const DmMetrics &m = g_dmMetrics;

	// --- geometry -------------------------------------------------
	st.WindowRounding = m.windowRounding;
	st.ChildRounding = m.childRounding;
	st.FrameRounding = m.frameRounding;
	st.GrabRounding = m.grabRounding;
	st.TabRounding = m.tabRounding;
	st.WindowBorderSize = m.windowBorderSize;
	st.FrameBorderSize = m.frameBorderSize;
	st.ChildBorderSize = 1.0f;
	st.PopupBorderSize = 1.0f;
	st.PopupRounding = m.childRounding;
	st.ScrollbarSize = m.scrollbarSize;
	st.ScrollbarRounding = 6.0f;
	st.GrabMinSize = 10.0f;

	st.ItemSpacing = m.itemSpacing;
	st.WindowPadding = m.windowPadding;
	st.FramePadding = m.framePadding;
	st.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
	st.CellPadding = ImVec2(6.0f, 4.0f);
	st.IndentSpacing = 18.0f;

	st.WindowTitleAlign = ImVec2(0.0f, 0.5f);
	st.ButtonTextAlign = ImVec2(0.5f, 0.5f);
	st.SelectableTextAlign = ImVec2(0.0f, 0.5f);
	st.DisabledAlpha = 0.50f;
	st.TabBorderSize = 0.0f;

#if IMGUI_VERSION_NUM >= 19000
	st.TabBarBorderSize = 1.0f;
#endif
#if IMGUI_VERSION_NUM >= 19060
	st.TabBarOverlineSize = 2.0f;
#endif
#if IMGUI_VERSION_NUM >= 18940
	st.MenuItemRounding = 4.0f;
#endif
#if IMGUI_VERSION_NUM >= 19200
	st.SeparatorSize = 1.0f;
	st.SeparatorTextBorderSize = 2.0f;
	st.TreeLinesSize = 1.0f;
	st.TreeLinesRounding = 3.0f;
#endif

	// --- colours --------------------------------------------------
	// Start from the built-in dark baseline so any new ImGuiCol_ in a
	// future ImGui still has a sane value, then override semantically.
	ImGui::StyleColorsDark();
	ImVec4 *c = st.Colors;

	c[ImGuiCol_Text]                  = DmCol(p.textPrimary);
	c[ImGuiCol_TextDisabled]          = DmCol(p.textDisabled);
	c[ImGuiCol_WindowBg]              = DmCol(p.bgDeep, 0.98f);
	c[ImGuiCol_ChildBg]               = DmCol(p.bgPanel, 0.62f);
	c[ImGuiCol_PopupBg]               = DmCol(p.bgPanel, 0.99f);
	c[ImGuiCol_Border]                = DmCol(p.border);
	c[ImGuiCol_BorderShadow]          = DmCol(p.shadow, 0.35f);

	c[ImGuiCol_FrameBg]               = DmCol(p.bgPanel);
	c[ImGuiCol_FrameBgHovered]        = DmCol(p.bgHover);
	c[ImGuiCol_FrameBgActive]         = DmCol(p.bgActive);

	c[ImGuiCol_TitleBg]               = DmCol(p.bgPanel);
	c[ImGuiCol_TitleBgActive]         = DmCol(p.bgRaised);
	c[ImGuiCol_TitleBgCollapsed]      = DmCol(p.bgDeep, 0.90f);
	c[ImGuiCol_MenuBarBg]             = DmCol(p.bgRaised);

	c[ImGuiCol_ScrollbarBg]           = DmCol(p.bgDeep, 0.60f);
	c[ImGuiCol_ScrollbarGrab]         = DmCol(p.borderLight, 0.85f);
	c[ImGuiCol_ScrollbarGrabHovered]  = DmCol(p.textDisabled);
	c[ImGuiCol_ScrollbarGrabActive]   = DmCol(p.accentDim);

	c[ImGuiCol_CheckMark]             = DmCol(p.accent);
	c[ImGuiCol_SliderGrab]            = DmCol(p.accentDim);
	c[ImGuiCol_SliderGrabActive]      = DmCol(p.accent);

	// Aero raised glass for chrome; the taxi-yellow accent is reserved
	// for selection state, so it never floods a full panel.
	c[ImGuiCol_Button]                = DmCol(p.bgRaised);
	c[ImGuiCol_ButtonHovered]         = DmCol(p.bgHover);
	c[ImGuiCol_ButtonActive]          = DmCol(p.bgActive);

	c[ImGuiCol_Header]                = DmCol(p.bgRaised);
	c[ImGuiCol_HeaderHovered]         = DmCol(p.bgHover);
	c[ImGuiCol_HeaderActive]          = DmCol(p.bgActive);

	c[ImGuiCol_Separator]             = DmCol(p.separator);
	c[ImGuiCol_SeparatorHovered]      = DmCol(p.accentDim);
	c[ImGuiCol_SeparatorActive]       = DmCol(p.accent);

	c[ImGuiCol_ResizeGrip]            = DmCol(p.border);
	c[ImGuiCol_ResizeGripHovered]     = DmCol(p.accentDim);
	c[ImGuiCol_ResizeGripActive]      = DmCol(p.accent);

	c[ImGuiCol_Tab]                   = DmCol(p.bgPanel);
	c[ImGuiCol_TabHovered]            = DmCol(p.bgHover);
#if IMGUI_VERSION_NUM >= 19006
	c[ImGuiCol_TabSelected]           = DmCol(p.bgRaised);
	c[ImGuiCol_TabDimmed]             = DmCol(p.bgDeep);
	c[ImGuiCol_TabDimmedSelected]     = DmCol(p.bgRaised);
	c[ImGuiCol_TabSelectedOverline]   = DmCol(p.accent);
	c[ImGuiCol_TabDimmedSelectedOverline] = DmCol(p.accentDim);
#else
	c[ImGuiCol_TabActive]             = DmCol(p.bgRaised);
	c[ImGuiCol_TabUnfocused]          = DmCol(p.bgDeep);
	c[ImGuiCol_TabUnfocusedActive]    = DmCol(p.bgRaised);
#endif

	c[ImGuiCol_PlotLines]             = DmCol(p.accent);
	c[ImGuiCol_PlotLinesHovered]      = DmCol(p.accentHover);
	c[ImGuiCol_PlotHistogram]         = DmCol(p.info);
	c[ImGuiCol_PlotHistogramHovered]  = DmCol(p.accent);

	c[ImGuiCol_TableHeaderBg]         = DmCol(p.bgRaised);
	c[ImGuiCol_TableBorderStrong]     = DmCol(p.borderLight);
	c[ImGuiCol_TableBorderLight]      = DmCol(p.separator);
	c[ImGuiCol_TableRowBg]            = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	c[ImGuiCol_TableRowBgAlt]         = DmCol(p.glassSheen, 0.025f);

	c[ImGuiCol_TextSelectedBg]        = DmCol(p.accent, 0.30f);
	c[ImGuiCol_TextLink]              = DmCol(p.info);
	c[ImGuiCol_DragDropTarget]        = DmCol(p.accent);
	c[ImGuiCol_NavCursor]             = DmCol(p.accent);
	c[ImGuiCol_NavWindowingHighlight] = DmCol(p.accent, 0.70f);
	c[ImGuiCol_NavWindowingDimBg]     = DmCol(p.shadow, 0.55f);
	c[ImGuiCol_ModalWindowDimBg]      = DmCol(p.shadow, 0.65f);

#if IMGUI_VERSION_NUM >= 19284
	// Newest 1.92 additions: keep them on-palette when present, skip
	// them on older headers instead of failing to compile.
	c[ImGuiCol_CheckboxSelectedBg]    = DmCol(p.accentDim, 0.55f);
	c[ImGuiCol_InputTextCursor]       = DmCol(p.accent);
	c[ImGuiCol_DragDropTargetBg]      = DmCol(p.accent, 0.12f);
	c[ImGuiCol_UnsavedMarker]         = DmCol(p.warn);
	c[ImGuiCol_TreeLines]             = DmCol(p.separator);
#endif
}

// ============================================================
// D3D9 device lifetime
//
// ImGui's DX9 backend stores its vertex/index buffers in
// D3DPOOL_DEFAULT and its font atlas as a device texture. Both die
// on IDirect3DDevice9::Reset, and submitting a draw list against a
// dead buffer crashes or renders garbage. depthhook.cpp's Reset
// wrapper invalidates before the real Reset and rebuilds after it.
// s_imGuiInitialized only turns true after a successful
// ImGui_ImplDX9_Init(), so the hooks are free before the menu has
// ever been opened.
// ============================================================
static bool s_imGuiInitialized = false;

bool
UI_ImGuiIsInitialized(void)
{
	return s_imGuiInitialized;
}

void
UI_ImGuiSetInitialized(bool initialized)
{
	// A renderer backend without a context cannot own device objects,
	// so never let the flag outrun CreateContext().
	s_imGuiInitialized = initialized && (ImGui::GetCurrentContext() != NULL);
}

void
UI_ImGuiInvalidateDeviceObjects(void)
{
	if(!s_imGuiInitialized || ImGui::GetCurrentContext() == NULL)
		return;
	// Receipt data must be read BEFORE the backend releases the texture: after
	// InvalidateDeviceObjects the ImTextureData carries TexID == Invalid.
	ImGuiIO &io = ImGui::GetIO();
	ImFontAtlas *atlas = io.Fonts;
	ImTextureData *tex = (atlas != NULL) ? atlas->TexData : NULL;
	const int w = (tex != NULL) ? tex->Width : 0;
	const int h = (tex != NULL) ? tex->Height : 0;
	const void *oldId = (tex != NULL) ? (void*)(intptr_t)tex->TexID : NULL;
	ImGui_ImplDX9_InvalidateDeviceObjects();
	dbglog("[ovl] font atlas invalidate %dx%d tex=%p released (pre-Reset)",
	       w, h, oldId);
}

void
UI_ImGuiCreateDeviceObjects(void)
{
	if(!s_imGuiInitialized || ImGui::GetCurrentContext() == NULL)
		return;
	ImGui_ImplDX9_CreateDeviceObjects();

	// ImGui 1.92's DX9 backend does no texture work in CreateDeviceObjects:
	// textures are created on demand in RenderDrawData. After Reset the font
	// atlas sits at WantCreate with TexID == Invalid (the backend's destroy
	// path flips Destroyed -> WantCreate because the dynamic atlas keeps its
	// CPU pixels). Recreate it HERE, on the freshly reset device, instead of
	// leaving every ImGui glyph untextured until the next frame's render. The
	// core would requeue it on the next NewFrame anyway, and this is
	// idempotent: a successful create sets Status = OK, which the core skips.
	ImGuiIO &io = ImGui::GetIO();
	ImFontAtlas *atlas = io.Fonts;
	ImTextureData *tex = (atlas != NULL) ? atlas->TexData : NULL;
	if(tex != NULL){
		const bool wantCreate = (tex->Status == ImTextureStatus_WantCreate);
		if(wantCreate && tex->Pixels != NULL)
			ImGui_ImplDX9_UpdateTexture(tex);
		dbglog("[ovl] font atlas recreate %dx%d tex=%p (post-Reset, status=%s)",
		       tex->Width, tex->Height, (void*)(intptr_t)tex->TexID,
		       (tex->TexID != ImTextureID_Invalid) ? "OK" : "FAILED");
	}else{
		dbglog("[ovl] font atlas recreate skipped (atlas not built yet)");
	}

	// Same palette, new device generation. Re-applying is a few dozen
	// plain struct writes: no allocation, no texture work.
	DebugMenu_ApplyTheme();
}

// ============================================================
// Glass header band
// ============================================================
void
DrawGlassHeader(const char* label, ImVec2 size)
{
	if(ImGui::GetCurrentContext() == NULL)
		return;
	if(label == NULL)
		label = "";

	const DmPalette &p = g_dmPalette;
	const DmMetrics &m = g_dmMetrics;
	const float fontH = ImGui::GetTextLineHeight();
	float w = (size.x > 0.0f) ? size.x : ImGui::GetContentRegionAvail().x;
	float h = (size.y > 0.0f) ? size.y : fontH + m.headerPadding.y * 2.0f;
	if(w < 8.0f || h < fontH + 2.0f)
		return;

	ImVec2 p0 = ImGui::GetCursorScreenPos();
	ImVec2 p1(p0.x + w, p0.y + h);
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const float r = m.headerRounding;

	// Base glass slab.
	dl->AddRectFilled(p0, p1, DmColU32(p.bgPanel), r);

	// Top sheen: white fading into the panel over the upper 60%. Inset
	// horizontally so the unrounded gradient corners never poke out.
	const float inset = 2.0f;
	if(w > inset * 2.0f + r * 2.0f){
		dl->AddRectFilledMultiColor(
			ImVec2(p0.x + inset + r * 0.5f, p0.y + 1.0f),
			ImVec2(p1.x - inset - r * 0.5f, p0.y + h * 0.62f),
			DmColU32(p.glassSheen, 0.10f), DmColU32(p.glassSheen, 0.10f),
			DmColU32(p.glassSheen, 0.0f),  DmColU32(p.glassSheen, 0.0f));
	}

	// 1px top highlight + 1px bottom border = Aero's bevel language.
	dl->AddLine(ImVec2(p0.x + r, p0.y + 0.5f), ImVec2(p1.x - r, p0.y + 0.5f),
		DmColU32(p.glassSheen, 0.16f), 1.0f);
	dl->AddLine(ImVec2(p0.x + r, p1.y - 0.5f), ImVec2(p1.x - r, p1.y - 0.5f),
		DmColU32(p.border), 1.0f);

	// Soft contact shadow under the band.
	dl->AddRectFilledMultiColor(
		ImVec2(p0.x + r, p1.y),
		ImVec2(p1.x - r, p1.y + 3.0f),
		DmColU32(p.shadow, 0.22f), DmColU32(p.shadow, 0.22f),
		DmColU32(p.shadow, 0.0f),  DmColU32(p.shadow, 0.0f));

	// Outer hairline.
#if IMGUI_VERSION_NUM >= 19208
	dl->AddRect(p0, p1, DmColU32(p.border, 0.55f), r, 1.0f);
#else
	dl->AddRect(p0, p1, DmColU32(p.border, 0.55f), r, 0, 1.0f);
#endif

	// Accent tab on the left: the 3px taxi-yellow "bookmark" that ties
	// every heading back to the GTA palette.
	const float tabInset = 4.0f;
	dl->AddRectFilled(ImVec2(p0.x + 0.0f, p0.y + tabInset),
		ImVec2(p0.x + 3.0f, p1.y - tabInset), DmColU32(p.accent), 1.5f);

	// Label - clipped so a long group name can never bleed into the
	// chevron or the next column.
	if(w > 26.0f){
		const ImVec2 textPos(p0.x + m.headerPadding.x,
			p0.y + (h - fontH) * 0.5f);
		dl->PushClipRect(ImVec2(p0.x + 6.0f, p0.y),
			ImVec2(p1.x - 8.0f, p1.y), true);
		dl->AddText(textPos, DmColU32(p.textPrimary), label);
		dl->PopClipRect();
	}

	ImGui::Dummy(ImVec2(w, h));
}

// ============================================================
// Collapsible glass group header
// - same visual band as DrawGlassHeader
// - real clickable item; open state lives in ImGuiStorage, so it
//   behaves like CollapsingHeader (default open) without the
//   default grey triangle styling.
// ============================================================
bool
DrawGlassCollapsingHeader(const char* label, bool defaultOpen)
{
	if(ImGui::GetCurrentContext() == NULL)
		return defaultOpen;
	if(label == NULL)
		label = "";

	const DmPalette &p = g_dmPalette;
	const DmMetrics &m = g_dmMetrics;
	ImGuiID id = ImGui::GetID(label);
	ImGuiStorage *store = ImGui::GetStateStorage();
	bool open = (store->GetInt(id, defaultOpen ? 1 : 0) != 0);

	const float h = ImGui::GetFrameHeight() + 4.0f;
	const float w = ImGui::GetContentRegionAvail().x;
	if(w < 8.0f || h < 8.0f)
		return open;
	ImVec2 pos = ImGui::GetCursorScreenPos();

	DrawGlassHeader(label, ImVec2(w, h));

	// Re-submit an invisible button over the band. The Dummy above does
	// not take clicks, so this is the only interactive item here.
	ImGui::SetCursorScreenPos(pos);
	bool clicked = ImGui::InvisibleButton(label, ImVec2(w, h));
	bool hovered = ImGui::IsItemHovered();
	if(clicked){
		open = !open;
		store->SetInt(id, open ? 1 : 0);
	}

	ImDrawList *dl = ImGui::GetWindowDrawList();
	const float cx = pos.x + w - 14.0f;
	const float cy = pos.y + h * 0.5f;
	const float s = 3.2f;
	const ImU32 chev = DmColU32(hovered ? p.accent : p.textSecondary);
	if(open)
		dl->AddTriangleFilled(ImVec2(cx - s, cy - s * 0.55f),
			ImVec2(cx + s, cy - s * 0.55f), ImVec2(cx, cy + s * 0.95f), chev);
	else
		dl->AddTriangleFilled(ImVec2(cx - s * 0.55f, cy - s),
			ImVec2(cx - s * 0.55f, cy + s), ImVec2(cx + s * 0.95f, cy), chev);

	if(hovered){
#if IMGUI_VERSION_NUM >= 19208
		dl->AddRect(pos, ImVec2(pos.x + w, pos.y + h),
			DmColU32(p.accentDim), m.headerRounding, 1.0f);
#else
		dl->AddRect(pos, ImVec2(pos.x + w, pos.y + h),
			DmColU32(p.accentDim), m.headerRounding, 0, 1.0f);
#endif
	}

	return open;
}

// ============================================================
// Accent separator - 1px hairline with a short taxi-yellow segment.
// ============================================================
void
DrawAccentSeparator(void)
{
	if(ImGui::GetCurrentContext() == NULL)
		return;

	const DmPalette &p = g_dmPalette;
	ImDrawList *dl = ImGui::GetWindowDrawList();
	ImVec2 p0 = ImGui::GetCursorScreenPos();
	float w = ImGui::GetContentRegionAvail().x;
	if(w < 2.0f)
		return;

	const float y = p0.y + 3.0f;
	const float seg = (w < 46.0f) ? (w * 0.5f) : 42.0f;
	dl->AddLine(ImVec2(p0.x, y), ImVec2(p0.x + w, y), DmColU32(p.separator), 1.0f);
	dl->AddLine(ImVec2(p0.x, y), ImVec2(p0.x + seg, y), DmColU32(p.accent), 2.0f);

	// Same footprint as ImGui::Separator() (ItemSpacing.y after a line).
	ImGui::Dummy(ImVec2(0.0f, 5.0f));
}

// ============================================================
// Status pills
// ============================================================
static const char *
BadgeLabel(DmBadgeKind kind)
{
	switch(kind){
	case DM_BADGE_OVERRIDDEN: return "OVERRIDDEN";
	case DM_BADGE_INACTIVE:   return "INACTIVE";
	case DM_BADGE_RESTART:    return "RESTART";
	case DM_BADGE_NEW:        return "NEW";
	case DM_BADGE_OK:         return "OK";
	case DM_BADGE_WARN:       return "WARN";
	case DM_BADGE_ERROR:      return "ERROR";
	}
	return "";
}

static ImU32
BadgeTint(DmBadgeKind kind)
{
	switch(kind){
	case DM_BADGE_OVERRIDDEN: return g_dmPalette.accentDim;
	case DM_BADGE_INACTIVE:   return g_dmPalette.textDisabled;
	case DM_BADGE_RESTART:    return g_dmPalette.warn;
	case DM_BADGE_NEW:        return g_dmPalette.info;
	case DM_BADGE_OK:         return g_dmPalette.ok;
	case DM_BADGE_WARN:       return g_dmPalette.warn;
	case DM_BADGE_ERROR:      return g_dmPalette.error;
	}
	return g_dmPalette.textSecondary;
}

// Pill label colour: the tint pulled toward textPrimary so small
// 11px glyphs stay legible on the dark glass.
static ImU32
BadgeText(DmBadgeKind kind)
{
	return DmMix(BadgeTint(kind), g_dmPalette.textPrimary, 0.30f);
}

ImVec2
StatusBadgeSize(DmBadgeKind kind)
{
	const ImVec2 ts = ImGui::CalcTextSize(BadgeLabel(kind));
	return ImVec2(ts.x + 12.0f, ts.y + 3.0f);
}

void
DrawStatusBadge(DmBadgeKind kind, float rightScreenX)
{
	if(ImGui::GetCurrentContext() == NULL)
		return;

	const char *text = BadgeLabel(kind);
	const ImU32 tint = BadgeTint(kind);
	const ImVec2 ts = ImGui::CalcTextSize(text);
	const float padX = 6.0f;
	const float h = ts.y + 3.0f;
	const float w = ts.x + padX * 2.0f;

	// Anchor to the line of the previous item (the row label / INI key).
	const ImVec2 lastMin = ImGui::GetItemRectMin();
	const ImVec2 lastMax = ImGui::GetItemRectMax();
	const float y = (lastMin.y + lastMax.y) * 0.5f - h * 0.5f;
	float x;
	if(rightScreenX > 0.0f){
		x = rightScreenX - w;
		if(x < lastMax.x + 8.0f)	// never draw over the text it annotates
			x = lastMax.x + 8.0f;
	}else{
		x = lastMax.x + 8.0f;
	}

	const ImVec2 a(x, y);
	const ImVec2 b(x + w, y + h);
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const float r = h * 0.5f;

	dl->AddRectFilled(a, b, DmColU32(tint, 0.14f), r);
#if IMGUI_VERSION_NUM >= 19208
	dl->AddRect(a, b, DmColU32(tint, 0.60f), r, 1.0f);
#else
	dl->AddRect(a, b, DmColU32(tint, 0.60f), r, 0, 1.0f);
#endif
	dl->AddText(ImVec2(a.x + padX, a.y + (h - ts.y) * 0.5f), BadgeText(kind), text);
}

// ============================================================
// Mode-ownership pill
// ============================================================
// A 10px "MODE" tag: same pill geometry as the state badges, but the tint is
// accentDim rather than a state colour. It is deliberately quieter than every
// status badge because it only means "the active profile seeded this value",
// not "something is wrong". The 10px size plus one extra px of vertical
// padding is the smallest the word stays readable at 1280 and 1920 without
// the tag growing taller than a status badge. No render target, atlas or
// state is touched.
static const float kModeBadgeFontSize = 10.0f;
static const float kModeBadgePadY = 5.0f;
static const char *const kModeBadgeText = "MODE";

ImVec2
ModeOwnedBadgeSize(void)
{
	ImFont *font = ImGui::GetFont();
	if(!font)
		return ImVec2(0.0f, 0.0f);
	const ImVec2 ts = font->CalcTextSizeA(kModeBadgeFontSize, 4096.0f, 0.0f, kModeBadgeText);
	return ImVec2(ts.x + 10.0f, kModeBadgeFontSize + kModeBadgePadY);
}

void
DrawModeOwnedBadge(const char *tip, float rightScreenX)
{
	if(ImGui::GetCurrentContext() == NULL)
		return;
	ImFont *font = ImGui::GetFont();
	if(!font)
		return;

	const ImU32 tint = g_dmPalette.accentDim;
	const ImVec2 ts = font->CalcTextSizeA(kModeBadgeFontSize, 4096.0f, 0.0f, kModeBadgeText);
	const float padX = 5.0f;
	const float h = kModeBadgeFontSize + kModeBadgePadY;
	const float w = ts.x + padX * 2.0f;

	// Anchor to the line of the previous item, exactly like DrawStatusBadge.
	const ImVec2 lastMin = ImGui::GetItemRectMin();
	const ImVec2 lastMax = ImGui::GetItemRectMax();
	const float y = (lastMin.y + lastMax.y) * 0.5f - h * 0.5f;
	float x;
	if(rightScreenX > 0.0f){
		x = rightScreenX - w;
		if(x < lastMax.x + 8.0f)	// never draw over the key text
			x = lastMax.x + 8.0f;
	}else{
		x = lastMax.x + 8.0f;
	}

	const ImVec2 a(x, y);
	const ImVec2 b(x + w, y + h);
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const float r = h * 0.5f;

	dl->AddRectFilled(a, b, DmColU32(tint, 0.16f), r);
#if IMGUI_VERSION_NUM >= 19208
	dl->AddRect(a, b, DmColU32(tint, 0.62f), r, 1.0f);
#else
	dl->AddRect(a, b, DmColU32(tint, 0.62f), r, 0, 1.0f);
#endif
	dl->AddText(font, kModeBadgeFontSize,
		ImVec2(a.x + padX, a.y + (h - ts.y) * 0.5f),
		DmColU32(DmMix(tint, g_dmPalette.textPrimary, 0.34f), 1.0f),
		kModeBadgeText);

	if(tip && ImGui::IsMouseHoveringRect(a, b) &&
	   ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
		ImGui::SetTooltip("%s", tip);
}
