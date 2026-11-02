#include "skygfx.h"
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "debugmenu_theme.h"	// Aero/Bento palette + glass draw helpers
#include "neopipe.h"
#include "modeManager.h"
#include "treeWind.h"	// W8-2: TreeAlpha_SetProfile (tree alpha profile row)
#include "profiler.h"	// pass/frame profiler: g_pfEnable / g_pfGpuTimers (Profiler tab)
#include "debug_handl.h"	// DebugFrameId()/DebugFrameCode() (debug-handl lane; TODO local decls below until they land)
#include <d3dx9.h>
#include <string.h>
#include <ctype.h>	// tolower — INI-key filter match
#include <string>
#include <vector>
#include <algorithm>

extern bool iCanHasvehiclePipe;
extern bool iCanHasSunGlare;
extern bool iCanHasNeoDrops;
extern char asipath[MAX_PATH];	// game dir with trailing '\', set by readIni
// main.cpp globals that are live but are not Config fields (readIniFile reads
// them directly, so the menu can edit the same variable the renderer sees).
extern bool cloudRenderFix;		// CClouds::Render state hygiene (INI cloudRenderFix)
// waterPipe.cpp: INI waterFresnelSplit — live global, but not written by
// saveConfigTo (the menu's INI writer covers it, see MenuWriteMissingIniKeys).
extern int g_waterFresnelSplit;
extern bool g_waterParallaxActive;
extern IDirect3DTexture9 *g_iblTex;	// postfx.cpp: dynamic-sky capture bound on s3
extern int g_fpGpuLightCount;	// forwardplus.cpp (debug readout)
extern int g_fpNumLights;		// forwardplus.cpp (debug readout)
// main.cpp (pipe-conjunction fix #4) — re-derives config->pipeline/
// colorFilter/ivMode from the actual building/vehicle pipes.
extern void derivePipelineFromPipes(Config *c);
// main.cpp: colorFilterEnable user-intent latch (silent-write self-heal).
// The row below records every toggle as USER INTENT so the per-frame guard
// never fights legitimate edits and the value persists across world entry.
extern void cfEnableSetUserIntent(Config *c, RwBool v);
static void
cfEnableRowIntent(void) { cfEnableSetUserIntent(config, config->colorFilterEnable); }

// Diagnostics row onEdit — the logger's throttle gates read g_diagLevel
// (debug_handl.cpp), which readIniFile syncs from the Config field only at
// INI load. Re-assert it here so a menu edit takes effect this session.
static void
diagLevelRowEdit(void) { g_diagLevel = config->diagLevel; }

// debugMenuOpen is UI state that happens to live in Config. ModeManager's
// apply/restore replaces the WHOLE Config from a per-slot snapshot taken when
// the mode was latched - often while this window was closed, so the snapshot
// carries debugMenuOpen == 0. Letting that land closed the settings window on
// the frame after any mode click. The menu owns this flag; keep it across the
// apply and let ModeManager own every render key. No heap, no D3D, no state.
struct ModeKeepMenuOpen
{
	RwBool saved;
	ModeKeepMenuOpen(void) : saved(config ? config->debugMenuOpen : 0) {}
	~ModeKeepMenuOpen(void) { if(config) config->debugMenuOpen = saved; }
};

// ---- Profiler snapshot surface (src/render/profiler.cpp) -------------------
// profiler.h carries only the instrument API (PF_SCOPE / PfFrame), so the
// readout surface used by this tab is declared here: plain C functions with
// caller-provided arrays, no struct has to be shared across TUs.
extern "C" {
	int  PfSnapshotNames(const char **names, float *cpuMs, float *cpuMin, float *cpuAvg,
	                     float *cpuMax, float *gpuMs, int max);
	void PfFrameInfo(float *ms, float *mn, float *avg, float *mx,
	                 long long *draws, long long *verts, long long *tris,
	                 int *psBinds, int *vsBinds);
	const float *PfFrameHistory(int *count);
	int  PfBindSnapshot(const char **tags, void **lastPS, void **lastVS,
	                    int *ps, int *vs, int max);
	int  PfNotesSnapshot(char *out, int outSize);
	void PfResetStats(void);
}

// Profiler row onEdit — the Config fields must reach the live globals the
// instrumented code reads (PF_SCOPE / PfFrame early-out on g_pfEnable).
static void
profilerEnableRowEdit(void)
{
	g_pfEnable = config->profilerEnable ? 1 : 0;
	if(dbglog_throttle("pf_row_enable"))
		dbglog("[Menu] profiler enable=%ld", g_pfEnable);
}

static void
profilerGpuRowEdit(void)
{
	g_pfGpuTimers = config->profilerGpuTimers ? 1 : 0;
	if(dbglog_throttle("pf_row_gpu"))
		dbglog("[Menu] profiler gpuTimers=%ld", g_pfGpuTimers);
}

// ============================================================
// Unified Pipeline — ImGui debug menu
// (the dead UploadUnifiedConstants constants-upload path was removed
// 2026-11-01, source-inventory §2 — the JuniorDjjr unified pipeline was
// never integrated)
// ============================================================

// Forward-declare ImGui's WndProc handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static bool unifiedImGuiInited = false;
static WNDPROC s_originalWndProc = nullptr;

static LRESULT CALLBACK ImGuiWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if(ImGui::GetCurrentContext()){
		// Always let ImGui track mouse/keyboard state
		if(ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)){
			// Only consume input when menu is open (config can be unbound
			// before readIniFile runs — the subclass outlives init).
			if(config && config->debugMenuOpen)
				return 0;
		}
	}
	return CallWindowProc(s_originalWndProc, hWnd, msg, wParam, lParam);
}

static void EnsureImGuiInit(IDirect3DDevice9 *device)
{
	if(unifiedImGuiInited) return;
	if(!device) return;
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.MouseDrawCursor = true;  // GTA SA hides OS cursor — ImGui draws its own
	// Foundation theme (palette, metrics, ImGuiCol table). Installed
	// before any NewFrame/Begin, exactly once, so every window, child,
	// popup, table and tooltip inherits the same Aero glass style.
	DebugMenu_ApplyTheme();
	// Tell the theme module the backend owns device objects now, so the
	// Reset wrapper in depthhook.cpp can invalidate/rebuild them. Only on
	// a successful init: the flag gates every ImGui device call.
	if(ImGui_ImplDX9_Init(device))
		UI_ImGuiSetInitialized(true);
	// Get game window handle for Win32 input
	D3DDEVICE_CREATION_PARAMETERS dcp;
	device->GetCreationParameters(&dcp);
	if(dcp.hFocusWindow){
		ImGui_ImplWin32_Init(dcp.hFocusWindow);
		// Subclass to forward input to ImGui
		s_originalWndProc = (WNDPROC)SetWindowLongPtr(dcp.hFocusWindow, GWL_WNDPROC, (LONG_PTR)ImGuiWndProc);
	}
	// One-shot lifecycle receipt. The font atlas itself is built lazily by
	// the first ImGui::NewFrame(), and its D3D9 texture is created inside
	// ImGui_ImplDX9_RenderDrawData() (in ImGui 1.92 the backend's
	// CreateDeviceObjects() does no texture work). Log the backend state so
	// the later "[ovl] font atlas WxH tex=..." line can be correlated with
	// this init, and make a missing atlas impossible to miss.
	dbglog("[ovl] imgui init: device=%p fonts=%p rendererHasTextures=%d window=%p",
	       (void*)device, (void*)io.Fonts,
	       (io.BackendFlags & ImGuiBackendFlags_RendererHasTextures) ? 1 : 0,
	       (void*)dcp.hFocusWindow);
	if(io.Fonts == NULL)
		dbglog("[ovl] font atlas MISSING after CreateContext (io.Fonts == NULL) - no ImGui text can render");
	unifiedImGuiInited = true;
}

// ============================================================
// Font-atlas diagnostics
// ============================================================
// One receipt per atlas texture generation (device-object creation): the
// first frame after init, and again after every DEVICE RESET recreate. The
// DX9 font texture is D3DPOOL_DEFAULT and dies on Reset; if the recreate
// path ever stops working, this line is the proof (tex=0000000000000000 or
// dims 0x0 / NOT READY) and every Menu/overlay glyph is garbage or blank.
static const void *s_ovLastAtlasTex = NULL;
static int s_ovLastAtlasW = 0;
static int s_ovLastAtlasH = 0;

static void
OvLogFontAtlas(const char *tag)
{
	if(ImGui::GetCurrentContext() == NULL)
		return;
	ImGuiIO &io = ImGui::GetIO();
	ImFontAtlas *atlas = io.Fonts;
	ImTextureData *tex = (atlas != NULL) ? atlas->TexData : NULL;
	const int w = (tex != NULL) ? tex->Width : 0;
	const int h = (tex != NULL) ? tex->Height : 0;
	const void *id = (tex != NULL) ? (void*)(intptr_t)tex->TexID : NULL;
	if(id == s_ovLastAtlasTex && w == s_ovLastAtlasW && h == s_ovLastAtlasH)
		return;	// same generation: stay one-shot
	s_ovLastAtlasTex = id;
	s_ovLastAtlasW = w;
	s_ovLastAtlasH = h;
	dbglog("[ovl] font atlas %dx%d tex=%p (%s, tag=%s fonts=%d font=%p)",
	       w, h, id,
	       (tex != NULL && w > 0 && h > 0 && id != NULL) ? "ready" : "NOT READY",
	       tag,
	       (atlas != NULL) ? atlas->Fonts.Size : 0,
	       (void*)ImGui::GetFont());
}

#define RB(x) reinterpret_cast<bool*>(&(x))

// F9 correlation-overlay runtime state, hoisted here so the Diagnostics
// section's row can bind it directly (DebugOverlayInit reads the INI key
// into this flag once at overlay init; DebugOverlayDraw polls it every
// frame, so a menu edit takes effect immediately). The Save INI writer
// (MenuWriteMissingIniKeys) persists the key, so it round-trips.
static bool s_ovEnabled     = false;    // boot value read from INI (debugOverlay, default 0)

// ============================================================
// Named configs: files under <game>\configs\*.ini
// (Save as New Config / Saved configs selector)
// ============================================================
static char s_cfgNameBuf[64] = "";
static std::vector<std::string> s_cfgList;
static std::string s_cfgSel;

// Global settings filter (hides non-matching rows on every tab). The INI
// Keys tab keeps its own input too; both filters combine there.
static char s_settingFilter[96] = "";
static char s_copyStatus[160] = "";
static double s_copyStatusTime = 0.0;

static void
RefreshCfgList(void)
{
	s_cfgList.clear();
	WIN32_FIND_DATAA fd;
	char pattern[MAX_PATH];
	snprintf(pattern, MAX_PATH, "%sconfigs\\*.ini", asipath);
	HANDLE h = FindFirstFileA(pattern, &fd);
	if(h == INVALID_HANDLE_VALUE)
		return;	// configs/ missing or empty — empty list, shown gracefully
	do{
		if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;
		std::string nm = fd.cFileName;
		size_t len = nm.size();
		if(len > 4 && _stricmp(nm.c_str() + len - 4, ".ini") == 0)
			nm.resize(len - 4);
		if(!nm.empty())
			s_cfgList.push_back(nm);
	}while(FindNextFileA(h, &fd));
	FindClose(h);
	std::sort(s_cfgList.begin(), s_cfgList.end());
}

static bool
IsValidCfgName(const char *s)
{
	size_t len = strlen(s);
	if(len == 0 || len >= sizeof(s_cfgNameBuf))
		return false;
	if(!strcmp(s, ".") || !strcmp(s, ".."))
		return false;
	// reject path separators / Windows-illegal filename characters
	return strpbrk(s, "\\/:*?\"<>|") == NULL;
}

// Custom Maxed resolution (Style combo): load the NEWEST file under
// <game>\configs\*.ini — the user's latest "Save as New Config". No
// configs directory yet (only skygfx.ini exists) => no-op: Custom Maxed
// = the current in-memory/INI state. loadConfigFile re-runs readIniFile
// (incl. the envFresnel/vehEnvIntensity clamps) + refreshIni.
static void
LoadNewestCustomConfig(void)
{
	WIN32_FIND_DATAA fd;
	char pattern[MAX_PATH];
	snprintf(pattern, MAX_PATH, "%sconfigs\\*.ini", asipath);
	HANDLE h = FindFirstFileA(pattern, &fd);
	if(h == INVALID_HANDLE_VALUE){
		if(dbglog_throttle("preset_custom"))
			dbglog("[Menu] Custom Maxed: no configs\\*.ini found - keeping current state");
		return;
	}
	char newestPath[MAX_PATH] = "";
	FILETIME newest;
	ZeroMemory(&newest, sizeof(newest));
	do{
		if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;
		size_t len = strlen(fd.cFileName);
		if(len <= 4 || _stricmp(fd.cFileName + len - 4, ".ini") != 0)
			continue;
		if(newestPath[0] == '\0' || CompareFileTime(&fd.ftLastWriteTime, &newest) > 0){
			snprintf(newestPath, MAX_PATH, "%sconfigs\\%s", asipath, fd.cFileName);
			newest = fd.ftLastWriteTime;
		}
	}while(FindNextFileA(h, &fd));
	FindClose(h);
	if(newestPath[0] == '\0'){
		if(dbglog_throttle("preset_custom"))
			dbglog("[Menu] Custom Maxed: configs\\ empty - keeping current state");
		return;
	}
	loadConfigFile(newestPath);	// may overwrite config->preset; caller sets PRESET_CUSTOM after
	if(dbglog_throttle("preset_custom"))
		dbglog("[Menu] Custom Maxed loaded newest config: %s", newestPath);
}

// ============================================================
// DEPLOYED-SETTINGS INVENTORY
//   Layout: ONE master-detail pair (no tab strips, nothing side-scrolls).
//   LEFT  s_menuSections — the section list, category-grouped
//     (Setup / Effects / World / Diagnostics / System / Reference), every
//     effect with a master switch carrying a colour-coded ON/OFF pill.
//   RIGHT the active section's pane: its SettingRow rows, or a custom body
//     (Switchboard, Presets, Profiler, INI Keys, All Settings).
//   The Switchboard section (tab "Main") carries the Unified pipeline ->
//   Rendering pipes -> Effects master switches -> Rendering features and
//   the folded detail groups (lighting, grass, sun, weather, dual-pass,
//   modulate, normal map/buffer, building reflection).
//   The PostFX section is the systematic post-processing checklist — ONE
//   individually-toggleable row per pass, grouped by effect, so a single
//   feature can be switched off at a time while hunting a full-screen black.
//   The Diagnostics section carries the debug readout switches (PostFX
//   dump, Forward+ debug view, F9 overlay, diagLevel verbosity).
//
// Goal (user): "I know what settings are actually deployed" — every key
// skygfx.ini can carry is listed in-game next to the INI key it maps to,
// and every edit writes the SAME variable readIniFile()/refreshIni() read,
// so F11 "Reload Inis" and this menu can never disagree.
//
// Rows whose INI key has NO live variable (startup-only patches, the
// load-time-only quality preset, keys readIniFile never parses) are listed
// GREYED so nothing is invisible.
//
// Rows are built lazily and REBUILT whenever `config` changes — `config`
// is a pointer that moves with the Config selector.
// ============================================================

// globals owned by main.cpp (non-static -> menu writes go through the very
// same variables readIniFile reads)
extern bool disableClouds, disableGamma, fixPcCarLight, privateHooks, forceWindShader;
extern int  transparentLockon, fixShadows;
extern int  explicitBuildingPipe_tmp;
// [General] NoiseQuality lives in a TU-static in main.cpp — accessors live
// in the debug-menu registration block of main.cpp.
extern int  menuGetNoiseQuality(void);
extern void menuSetNoiseQuality(int q);

enum RowKind {
	RK_BOOL,		// int-sized bool (RwBool / int 0|1) -> checkbox
	RK_BOOL8,		// 1-byte bool -> checkbox
	RK_INT,			// int -> slider
	RK_FLOAT,		// float -> slider
	RK_ENUM,		// int -> combo, value = base + index
	RK_STATIC,		// int shown GREYED (no live edit path)
	RK_STATICB,		// bool shown GREYED
	RK_NONE,		// no variable at all -> GREYED
	RK_NOISEQ,		// [General] NoiseQuality -> main.cpp accessors
	RK_INIINT		// startup-only int read from the current INI (no live variable)
};

struct SettingRow {
	const char *tab;	// tab this row lives on
	const char *grp;	// collapsing header inside that tab
	const char *key;	// exact INI key
	const char *label;	// UI label
	RowKind     kind;
	void       *ptr;	// NULL => no live variable
	float       lo, hi;
	const char *const *names;	// NULL-terminated (RK_ENUM)
	int         base;			// value of names[0]
	void      (*onEdit)(void);	// extra write-through after a change
	const char *note;			// tooltip extra
};

// --- enum label tables (exact spellings readIniFile's StrAssoc maps use) ---
static const char *const s_pipeNames[]   = { "PBR", "PS2", "Xbox", "Mobile", "GTAIV", NULL };
static const char *const s_buildNames[]  = { "PS2", "Xbox", "GTAIV", "PBR", NULL };
static const char *const s_vehNames[]    = { "PS2", "PC", "Xbox", "Specular", "Mobile", "Neo", "Leeds", "VCS", "Env", "GTAIV", "Modern", NULL };
static const char *const s_cfNames[]     = { "None", "PS2", "PC", "Mobile", "III", "VC", "VCS", "GTAIV", "Modern", NULL };
static const char *const s_waterSty[]    = { "Xbox", "IV", "V", NULL };
static const char *const s_waterQ[]      = { "Low", "Medium", "High", "Ultra", NULL };
static const char *const s_ps2pc[]       = { "PS2", "PC", NULL };
static const char *const s_shadow[]      = { "Default (key absent)", "0 / PS2", "1 / PC", NULL };	// base -1
static const char *const s_corona[]      = { "- (leave alone)", "PS2 (disable z-test)", "PC (force z-test)", NULL };	// base -1
static const char *const s_pedCull[]     = { "Default (leave game)", "0 (same as Default)", "CULLNONE", "CULLBACK", "CULLFRONT", NULL };	// base -1
static const char *const s_rad[]         = { "PS2", "Shader", NULL };
static const char *const s_lightning[]   = { "Sky only", "Sky and objects", NULL };
static const char *const s_noise[]       = { "32 px", "128 px", "512 px", "1024 px", NULL };
// New post-effects: tonemapOperator (0 filmic .. 5 GT7) + bloomQuality (0-3)
static const char *const s_tonemapOps[]  = { "Filmic", "ACES 1.3", "ACES 2.0", "AgX", "Frostbite", "GT7", NULL };
static const char *const s_bloomQ[]      = { "Low (0)", "Medium (1)", "High (2)", "Ultra (3)", NULL };
// W8-2 tree alpha profile — TreeAlpha_SetProfile (0 Off, 1 PC, 2 PC+, 3 Console)
static const char *const s_treeFx[]      = { "Off (0)", "PC (1)", "PC+ (2)", "Console (3)", NULL };

// ============================================================
// Family-aware ivMode sync — the menu's PIPE-writing paths only.
//
// ivMode is owned by the mode manager (whole-Config snapshots) and by
// explicit user writes (the IV-mode row, the INI key): derivePipelineFromPipes
// deliberately never re-derives it (main.cpp:599-602). What no one owned is
// the CONSEQUENCE of a pipe edit: DrawIVGrade gates on ivMode ALONE
// (postfx_colourfilter.cpp:1187) while the building pipe engages the IV
// shader path on `ivMode || buildingPipe == BUILDING_GTAIV`
// (buildingPipe.cpp:849), so an edit that crosses the IV family boundary
// leaves the two halves disagreeing:
//    pipes -> IV family : IV shaders on, IV grade off (missing grade)
//    pipes -> non-IV    : IV grade still compositing over PS2/Xbox/PBR
// This repairs ONLY that transition — mirroring readIniFile's family-aware
// ivMode default (main.cpp:3487) — and never touches ivMode when the family
// did not move, so a deliberate grade-off with IV pipes stays legal and the
// mode manager keeps sole ownership of mode entry/exit.
// ============================================================

// IV family = the two derivePipelineFromPipes rules that yield
// PIPELINE_GTAIV (rule 1: building=GTAIV; rule 2: vehicle=GTAIV with a
// PS2-ish building). Computed WITHOUT writing config->pipeline, so it is safe
// to call BEFORE a combo edit: derivePipelineFromPipes() would re-derive the
// pipeline from the still-old pipes and revert the user's selection.
static bool
PipeFamilyIsIV(void)
{
	if(!config)
		return false;
	int bp = config->buildingPipe, vp = config->vehiclePipe;
	return bp == BUILDING_GTAIV ||
	       (vp == CAR_GTAIV && (bp == BUILDING_PS2 || bp == BUILDING_XBOX));
}

// Family as of the START of the menu frame — captured in DrawUnifiedDebugMenu
// before any widget writes config. The raw pipe rows need it because their
// onEdit runs AFTER the row already stored the new pipe value, so the
// pre-edit family is no longer reconstructible inside the hook.
static bool s_pipeFamilyWasIV = false;

// wasIV = PipeFamilyIsIV() from BEFORE the edit (ApplyPipelineFromMenu
// captures it itself; the raw rows use the frame snapshot above).
static void
SyncIvModeFromPipeFamily(bool wasIV)
{
	if(!config)
		return;
	bool isIV = PipeFamilyIsIV();
	if(isIV == wasIV)
		return;			// family unchanged — the explicit ivMode write wins
	int want = isIV ? 1 : 0;
	if(config->ivMode == want)
		return;
	if(dbglog_throttle("ivmode_pipesync"))
		dbglog("[Menu] pipe family -> %s: ivMode %d -> %d (grade follows the pipes)",
			isIV ? "GTAIV" : "non-IV", (int)config->ivMode, want);
	config->ivMode = want;
}

// pipeline -> pipes mapping, byte-for-byte the switch readIniFile applies to
// the `pipeline` key. Without this the combo would write a value
// derivePipelineFromPipes() re-derives away on the next frame.
static void
ApplyPipelineFromMenu(void)
{
	// Family BEFORE the remap: the pipes still hold the previous selection
	// here (the switch below is what writes them).
	bool wasIV = PipeFamilyIsIV();

	switch(config->pipeline){
	case PIPELINE_PBR:
		config->buildingPipe = BUILDING_PBR;	config->vehiclePipe = CAR_MODERN;
		config->colorFilter = COLORFILTER_MODERN;
		break;
	case PIPELINE_PS2:
		config->buildingPipe = BUILDING_PS2;	config->vehiclePipe = CAR_PS2;
		break;
	case PIPELINE_XBOX:
		config->buildingPipe = BUILDING_XBOX;	config->vehiclePipe = CAR_XBOX;
		break;
	case PIPELINE_MOBILE:
		config->buildingPipe = BUILDING_XBOX;	config->vehiclePipe = CAR_MOBILE;
		break;
	case PIPELINE_GTAIV:
		config->buildingPipe = BUILDING_GTAIV;	config->vehiclePipe = CAR_GTAIV;
		break;
	default:
		config->buildingPipe = BUILDING_PBR;	config->vehiclePipe = CAR_MODERN;
		break;
	}

	// The combo is a pipe-family switch, so ivMode must follow it: selecting
	// "GTA IV" engages the IV shaders and needs the IV grade, anything else
	// must not leave the grade compositing over non-IV pipes.
	SyncIvModeFromPipeFamily(wasIV);
}

// Raw buildingPipe/vehiclePipe rows' onEdit — the row already stored the new
// pipe, so compare against the family snapshotted at the start of this frame
// (before any widget ran).
static void
PipeRowEdit(void)
{
	SyncIvModeFromPipeFamily(s_pipeFamilyWasIV);
}

static void
ApplyPipelineOverrideFromMenu(void)
{
	// same gate readIniFile applies (0=PBR 1=PS2 2=Xbox 3=Mobile 4=GTAIV,
	// -1 = disabled); -1 keeps whatever the pipes say.
	if(config->pipelineOverride >= 0 && config->pipelineOverride <= 4){
		config->pipeline = config->pipelineOverride;
		ApplyPipelineFromMenu();
	}
}

// IV-mode row onEdit: the toggle is routed through the mode manager, the
// single owner of the IV preset (modeManager.cpp profileGTAIV).
//   ON  -> ModeManager_SetMode(MODE_GTAIV): snapshot the live config, apply
//          the IV pipe/filter set on top (building=GTAIV vehicle=GTAIV
//          colour filter=GTAIV, ivMode on). The INI-tuned grade sliders ride
//          the snapshot untouched (same non-stomping rule as before).
//   OFF -> the same-id SetMode call toggles the mode back off: the pre-mode
//          config is restored verbatim, then the ivMode key is re-asserted 0
//          (the row's own write) so the restore cannot light the grade back on.
// One mode max — never stacks, and g_preModeConfig is never written from here.
static void
ivModeRowEdit(void)
{
	ModeKeepMenuOpen keepMenu;	// SetMode below replaces the Config; keep UI state
	if(config->ivMode){
		if(ModeManager_GetMode() != MODE_GTAIV)
			ModeManager_SetMode(MODE_GTAIV);	// NONE->A / A->B: profile on the pre-mode snapshot
		else if(!PipeFamilyIsIV()){
			// The mode is still latched but a pipe/combo edit has since moved
			// the pipes off the IV family (SyncIvModeFromPipeFamily dropped
			// ivMode to 0; this row's write just put it back). Re-apply the
			// profile so ON means "IV pipes + grade" again — otherwise the
			// IV grade would composite over PS2/Xbox/PBR pipes, the exact
			// desync the family sync exists to prevent. Same-id SetMode would
			// TOGGLE OFF instead, so the off/on pair is explicit.
			ModeManager_SetMode(MODE_NONE);		// restore the pre-mode snapshot
			ModeManager_SetMode(MODE_GTAIV);	// re-apply the profile on top of it
			if(dbglog_throttle("ivmode_reapply"))
				dbglog("[Menu] IV Mode ON -> MODE_GTAIV re-applied (pipes had left the IV family)");
		}
		if(dbglog_throttle("ivmode_on"))
			dbglog("[Menu] IV Mode ON -> ModeManager MODE_GTAIV (IV pipes+filter preset applied)");
	}else if(ModeManager_GetMode() == MODE_GTAIV){
		ModeManager_SetMode(MODE_GTAIV);	// same id = toggle off, pre-mode config restored
		config->ivMode = 0;	// the row's write survives the whole-config restore
		if(dbglog_throttle("ivmode_off"))
			dbglog("[Menu] IV Mode OFF -> ModeManager mode cleared (pre-mode config restored)");
	}
}

static void DrawEffectiveSummary(void);	// defined below (effective-value readout)

// W8-2 tree alpha profile row onEdit — forwards the Config value into
// treeWind.cpp's TreeAlpha_SetProfile (0 Off, 1 PC, 2 PC+, 3 Console), which
// sets TreeAlpha_profile / TreeAlpha_alphamul. Applied immediately: the CPU
// alpha-test ref moves in every pipe (TreeAlpha_ApplyAlphaRef) and the
// TreeAlpha.hlsl PS binds on the Console profile (TreeAlpha_Active).
static void
treeFxRowEdit(void)
{
	TreeAlpha_SetProfile(config->treeFx);
	if(dbglog_throttle("treefx_menu"))
		dbglog("[Menu] tree alpha profile=%d -> TreeAlpha_SetProfile", config->treeFx);
}

// ============================================================
// Startup-only INI keys with no Config field (currently
// forwardPlusFallback, read once by forwardplus.cpp's FpFallbackEnabled).
// They are shown read-only with the value in the active INI file so a
// developer can see what the next launch will use instead of guessing.
// ============================================================
static bool
CurrentIniPath(char *out, int outSize)
{
	if(!out || outSize <= 0 || asipath[0] == '\0')
		return false;
	if(currentConfig > 0)
		snprintf(out, outSize, "%sskygfx%d.ini", asipath, currentConfig);
	else
		snprintf(out, outSize, "%sskygfx.ini", asipath);
	if(GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES)
		return true;
	// Older builds only had skygfx.ini; fall back so the row still shows a
	// real value instead of "missing".
	snprintf(out, outSize, "%sskygfx.ini", asipath);
	return GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES;
}

static int
ReadCurrentIniInt(const char *key, int def)
{
	// The coverage/diagnostics views call this every frame for startup-only
	// keys; cache for 2 s so the menu never turns into a per-frame disk read.
	static char cachedKey[64] = "";
	static int cachedVal = 0;
	static DWORD cachedTick = 0;
	DWORD now = GetTickCount();
	if(strcmp(cachedKey, key) != 0 || now - cachedTick > 2000){
		char path[MAX_PATH];
		int v = def;
		if(CurrentIniPath(path, sizeof(path)))
			v = (int)GetPrivateProfileIntA("SkyGfx", key, def, path);
		strncpy(cachedKey, key, sizeof(cachedKey) - 1);
		cachedKey[sizeof(cachedKey) - 1] = '\0';
		cachedVal = v;
		cachedTick = now;
	}
	return cachedVal;
}

static std::string
CurrentIniPathString(void)
{
	char path[MAX_PATH];
	if(CurrentIniPath(path, sizeof(path)))
		return std::string(path);
	return std::string("(INI path unavailable)");
}

static std::vector<SettingRow>
BuildSettingRows(void)
{
	std::vector<SettingRow> v;
	const char *tab = "";
	const char *grp = "";

	#define G(T, GR)	do { tab = (T); grp = (GR); } while(0)
	#define A(K,L,KD,P,LO,HI,NM,BS,OE,NT) \
		v.push_back(SettingRow{ tab, grp, (K), (L), (KD), (void*)(P), (float)(LO), (float)(HI), (NM), (BS), (OE), (NT) })
	#define rW(K,L,P)					A(K,L,RK_BOOL,(P),0,0,NULL,0,NULL,NULL)
	#define rW8(K,L,P)					A(K,L,RK_BOOL8,(P),0,0,NULL,0,NULL,NULL)
	#define rW8n(K,L,P,NT)				A(K,L,RK_BOOL8,(P),0,0,NULL,0,NULL,(NT))
	#define rI(K,L,P,LO,HI)				A(K,L,RK_INT,(P),(LO),(HI),NULL,0,NULL,NULL)
	#define rF(K,L,P,LO,HI)				A(K,L,RK_FLOAT,(P),(LO),(HI),NULL,0,NULL,NULL)
	#define rE(K,L,P,NM,BS)				A(K,L,RK_ENUM,(P),0,0,(NM),(BS),NULL,NULL)
	#define rS(K,L,P)						A(K,L,RK_STATIC,(P),0,0,NULL,0,NULL,NULL)
	#define rS8(K,L,P)						A(K,L,RK_STATICB,(P),0,0,NULL,0,NULL,NULL)
	#define rN(K,L,NT)						A(K,L,RK_NONE,NULL,0,0,NULL,0,NULL,(NT))
	#define rWn(K,L,P,NT)					A(K,L,RK_BOOL,(P),0,0,NULL,0,NULL,(NT))
	#define rIn(K,L,P,LO,HI,NT)			A(K,L,RK_INT,(P),(LO),(HI),NULL,0,NULL,(NT))
	#define rFn(K,L,P,LO,HI,NT)			A(K,L,RK_FLOAT,(P),(LO),(HI),NULL,0,NULL,(NT))
	#define rEn(K,L,P,NM,BS,NT)			A(K,L,RK_ENUM,(P),0,0,(NM),(BS),NULL,(NT))
	#define rSn(K,L,P,NT)					A(K,L,RK_STATIC,(P),0,0,NULL,0,NULL,(NT))
	#define rIo(K,L,P,LO,HI,OE,NT)		A(K,L,RK_INT,(P),(LO),(HI),NULL,0,(OE),(NT))
	#define rEo(K,L,P,NM,BS,OE,NT)		A(K,L,RK_ENUM,(P),0,0,(NM),(BS),(OE),(NT))

	// ================= TAB: Main — the end-user switchboard =================
	// Top to bottom: Unified pipeline -> Rendering pipes -> Effects. Every
	// effect that can be switched off has its master toggle in this section;
	// the detailed sliders for each one live on the matching EFFECTS section
	// in the master-detail list (s_menuSections). Those sections show an
	// OFF pill while their effect is off but stay selectable, so settings
	// can be tuned before switching the effect on.
	G("Main", "Unified pipeline");
	rEo("pipeline", "Unified pipeline", &config->pipeline, s_pipeNames, 0, ApplyPipelineFromMenu,
		"readIniFile maps this key onto buildingPipe+vehiclePipe+colorFilter;\nthe menu applies the identical mapping, then derivePipelineFromPipes()\nre-derives it from the pipes each frame.\nivMode follows the pipe family (IV on <-> GTAIV pipes) — see\nSyncIvModeFromPipeFamily.");
	rIo("pipelineOverride", "Pipeline override", &config->pipelineOverride, -1, 4, ApplyPipelineOverrideFromMenu,
		"-1 = off. 0=PBR 1=PS2 2=Xbox 3=Mobile 4=GTAIV - forces `pipeline` at load;\nhere it applies the same mapping immediately.");
	rN("qualityPreset", "Quality preset",
		"LOAD-TIME ONLY: seeds feature defaults before individual keys override them.\nreadIniFile reads it into a local - there is no live Config field to edit.");
	rSn("gamePreset", "Game preset (index)", &config->preset,
		"config->preset - written by the Style combo / presets and saved as gamePreset.\nRaw index shown; use the Style combo on the Presets tab to change it.");

	G("Main", "Rendering pipes");
	rEo("buildingPipe", "Building pipe", &config->buildingPipe, s_buildNames, 0, PipeRowEdit,
		"Raw pipe edit: ivMode is re-synced ONLY if this change crosses the IV\nfamily boundary (grade follows the pipes); otherwise the explicit ivMode\nvalue is left alone.");
	rEo("vehiclePipe", "Vehicle pipe", &config->vehiclePipe, s_vehNames, 0, PipeRowEdit,
		"Raw pipe edit: ivMode is re-synced ONLY if this change crosses the IV\nfamily boundary (grade follows the pipes); otherwise the explicit ivMode\nvalue is left alone.");
	rE("waterStyle", "Water style", &config->waterStyle, s_waterSty, 0);
	rE("colorFilter", "Colour filter", &config->colorFilter, s_cfNames, 0);
	// colorFilterEnable gate: bound here in the Effects group (it used to be
	// duplicated as a second identical row — fix-6). The PostFX tab carries a
	// mirror of the same row with the same intent latch.

	// --- Effects: the master switchboard. One row per switchable effect;
	// the comment names the tab that carries that effect's detail sliders.
	G("Main", "Effects");
	rW("ssaoEnable", "SSAO", &config->ssaoEnable);			// mirror: SSAO tab
	rW("smaaEnable", "SMAA", &config->smaaEnable);			// mirror: SMAA tab
	rW("godRaysEnable", "God rays", &config->godRaysEnable);
	rW("heightFogEnable", "Height fog", &config->heightFogEnable);
	rW("motionBlurEnable", "Motion blur", &config->motionBlurEnable);
	rW("doRadiosity", "Radiosity", &config->doRadiosity);
	rW("radiosityEnable", "Radiosity (post-processing)", &config->radiosityEnable);
	A("colorFilterEnable", "Use colour filter", RK_BOOL, &config->colorFilterEnable, 0, 0, NULL, 0, cfEnableRowIntent,
		"Postfx switch for the colour-filter pass (which filter is used is the\n"
		"Colour filter dropdown on Rendering pipes).\n"
		"0 = bypass the pass entirely (black-screen isolation). Toggles record\n"
		"user intent via main.cpp's latch (silent-write self-heal).\n"
		"Mirrored on the PostFX tab with the same latch.");
	A("ivMode", "IV mode", RK_BOOL, &config->ivMode, 0, 0, NULL, 0, ivModeRowEdit,
		"Toggling ON enters ModeManager MODE_GTAIV: the IV pipe/filter preset (building=GTAIV,\n"
		"vehicle=GTAIV, colour filter=GTAIV, ivMode on) is applied on a snapshot of the live\n"
		"config, so the grade sliders on the IV grade tab are NOT reset. Toggling OFF leaves the\n"
		"mode (pre-mode config restored, ivMode back to 0).\n"
		"Mirrored on the PostFX tab with the same onEdit.");
	rW("grainEnable", "Film grain", &config->grainEnable);
	rW("sssEnable", "Screen-space SSS", &config->sssEnable);	// mirror: SSS tab
	rW("sssPostProcessEnable", "SSS post-process blur", &config->sssPostProcessEnable);
	rW("vcsTrails", "VCS trails", &config->vcsTrails);
	rWn("pipeChainEnable", "Pipe chain / SSR", &config->pipeChainEnable,
		"BRDF-classified screen-space reflections packed by the geometry pipes.");
	rWn("waterParallaxEnable", "Water parallax", &config->waterParallaxEnable,
		"Master switch (its scale and the rest of the water look live on the Water tab).");
	rWn("sunGlare", "Sun glare", &config->doglare,
		"config->doglare - hook installed at startup when the game has sun glare.");
	rE("infraredVision", "Infrared vision", &config->infraredVision, s_ps2pc, 0);
	rE("nightVision", "Night vision", &config->nightVision, s_ps2pc, 0);
	rW("neoWaterDrops", "Neo water drops", &config->neoWaterDrops);
	rW("neoBloodDrops", "Neo blood drops", &config->neoBloodDrops);

	G("Main", "Rendering features");
	rW("forwardPlusEnable", "Forward+ lighting", &config->forwardPlusEnable);
	// forwardPlusDebug retagged to the Diagnostics section (was here).
	rWn("normalMapEnable", "Normal mapping", &config->normalMapEnable,
		"Needs the RW normal-map plugin (gHasExternalNormalMapPlugin).");
	rWn("normalBufferEnable", "Normal buffer", &config->normalBufferEnable,
		"Screen-space normal target (SSAO/SSR input).");
	// velocityBufferEnable retagged to the Motion blur section (its buffer
	// feeds the motion-blur reprojection).
	rW("detailMaps", "Detail maps", &config->detailMaps);
	rWn("noiseDetail", "Noise-detail texturing", &config->noiseDetail,
		"config->noiseDetail - blue-noise jitter on tiled detail textures to hide repeats.");
	rW("depthHookEnable", "Depth buffer hook", &config->depthHookEnable);
	// ragdollEnable retagged to the Peds section.
	rWn("dualPass", "Dual-pass global", &config->dualPassGlobal,
		"Master; the per-type rows and alpha thresholds are in the Dual-pass detail group below.");
	rWn("ps2Modulate", "PS2 modulate global", &config->ps2ModulateGlobal,
		"Master; the per-type rows are in the PS2 modulate detail group below.");
	rW("usePCTimecyc", "PC timecyc", &config->usePCTimecyc);

	// State-integrity lane — render-state repairs at the frame / pipe
	// boundaries. Started as the three keys read at main.cpp:4513-4527; the
	// m0970 bisect block below adds the shields that used to be compiled ON
	// with no gate. All are 0/1 (clamped at parse) and live: every consumer
	// reads the Config field directly, so toggling here changes the next frame.
	G("Main", "State integrity");
	rWn("texAlphaClassify", "Texture alpha classification", &config->texAlphaClassify,
		"1 = classify texture alpha into the PBR alpha channel instead of\n"
		"relying on vertex alpha alone (fixes cutout foliage/decals).\n"
		"0 = legacy vertex-alpha-only path. Clamped [0..1] by readIniFile.");
	rWn("stateBaselineEnable", "Frame state baseline", &config->stateBaselineEnable,
		"1 = pipeApplyWorldBaseline at the four frame boundaries (RenderScene entry,\n"
		"RenderEffects exit, CPostEffects::Render exit, DrawFinalEffects) so every\n"
		"frame starts from the canonical three-layer world state. 0 = off.");
	rWn("defaultCbShaderHygiene", "Default-callback shader hygiene", &config->defaultCbShaderHygiene,
		"1 = unbind stale VS/PS in myDefaultCallback before a default-pipeline\n"
		"atomic draws (RW value-compare caches otherwise inherit the previous\n"
		"pass's shader). 0 = legacy behaviour.");
	rW8n("cloudRenderFix", "Cloud render-state hygiene", &cloudRenderFix,
		"main.cpp CClouds::Render hook: forces texture filter / alpha test / fog\n"
		"to known values for the cloud + corona draw, then restores them.\n"
		"Live for this session. Save INI covers this key (it was previously\n"
		"read on load but never written back). 0 = vanilla inherited states.");
	// m0970 bisect lane — render-state systems that were compiled ON with no
	// gate at all. Every default is 1 (= current behaviour), so nothing
	// renders differently until one is switched off. Turn them off one at a
	// time to bisect renderer-wide corruption; each note names what it gates.
	// (Retagged to the Shields section — see the same group there.)
	G("Shields", "Render-state bisect levers");
	rWn("shadowContractEnable", "Shadow state contract", &config->shadowContractEnable,
		"Gates the three-layer shadow render-state contract around the CShadows / "
		"realtime-shadow passes (shadowContractRun). Default 0 = vanilla shadow "
		"passes. Bisect lever.");
	rWn("spriteStateShield", "Sprite state shield (4-state)", &config->spriteStateShield,
		"Gates the per-draw ALPHAFUNC/ALPHAREF/SEPARATEALPHA/COLORWRITEENABLE "
		"normalisation inside coronaDrawRawShield (the blend force has its own "
		"row below). 0 = skip those four writes. Bisect lever.");
	rWn("coronaBlendShield", "Corona blend shield", &config->coronaBlendShield,
		"Gates the ALPHABLENDENABLE/SRCBLEND/DESTBLEND/ZWRITE/BLENDOP force in "
		"coronaDrawRawShield — the corona/point-light/cloud black-quad repair. "
		"0 = authored blend left untouched. Bisect lever.");
	rWn("spriteFlushShield", "Sprite-flush shield", &config->spriteFlushShield,
		"Gates the additive-block force at EVERY non-empty sprite flush plus the "
		"window-close canonical re-sync (SpriteFlushIndexedDraw_hook). "
		"0 = vanilla batch state. Bisect lever.");
	rWn("effectsAlphaBlock", "Effects alpha block", &config->effectsAlphaBlock,
		"Gates effectsForceAlphaBlock: the canonical alpha sweep at RenderEffects "
		"enter/exit, PostFX exit, both corona reflection hooks and CClouds. "
		"0 = no sweep. Bisect lever.");
	rWn("camRasterRepair", "Camera-raster repair", &config->camRasterRepair,
		"Gates the scene-entry repair that replaces a non-screen camera raster "
		"with the game's (m0170/m0171 blank-world fix); detection still logs. "
		"0 = swap left live. Bisect lever.");
	rWn("moonMaskAlpha", "Moon-mask alpha bracket", &config->moonMaskAlpha,
		"Gates the SEPARATEALPHA/ALPHAFUNC bracket around the 0x70D000 moon-mask "
		"draw. 0 = game alpha states kept. Bisect lever.");
	rWn("skidmarkAlphaFix", "Skidmark alpha fix", &config->skidmarkAlphaFix,
		"Gates the alpha-test force + restore around the skidmark draw "
		"(pipeForceAlphaBlock). 0 = inherited alpha state. Bisect lever.");

	// --- ped/shadow family retag: pedShadows/stencilShadows live on the Peds
	// section; shadowContractEnable on the Shields section.
	G("Peds", "Ped shadows");
	rEn("pedShadows", "Ped shadows", &config->pedShadows, s_shadow, -1,
		"INI accepts 0/1 (readIni's boolMap). The deployed -1 sentinel means\n"
		"'leave the game default alone'; other values are clamped to [-1..1].");
	rEn("stencilShadows", "Stencil shadows", &config->stencilShadows, s_shadow, -1,
		"INI accepts 0/1. -1 = leave the game default alone; values are\nclamped to [-1..1] by readIniFile.");

	// m0970 bisect lane — ped-path render-state systems, all INI keys that
	// previously had no menu row (defaults = the compiled behaviour).
	G("Peds", "Ped render-state contracts");
	rWn("pedOpaqueContract", "Ped opaque contract", &config->pedOpaqueContract,
		"Gates the blend-off / zwrite-on / GREATEREQUAL@1 contract forced for the "
		"skin pipeline in myDefaultCallback. Default 1 = contract on. Bisect lever.");
	rWn("pedAlphaTestEnable", "Ped alpha test", &config->pedAlphaTestEnable,
		"Gates the canonical GREATEREQUAL@1 alpha test inside the ped contract "
		"(0 = alpha test off for peds). Default 1 = on. Bisect lever.");
	rEn("pedCullMode", "Ped cull mode", &config->pedCullMode, s_pedCull, -1,
		"Forces CULLMODE through all three layers for the ped span. Default -1 = "
		"leave the game's raw cull; 0 is accepted by the menu but behaves as -1 "
		"at parse and at the gate. Bisect lever.");
	rFn("pedAmbientFloor", "Ped ambient floor", &config->pedAmbientFloor, 0.0f, 1.0f,
		"Minimum ambient luminance for the ped path (myDefaultCallback), default "
		"0.20 clamped [0..1]; 0 disables the lift. Bisect lever.");
	rWn("pedTexResync", "Ped stage-0 texture resync", &config->pedTexResync,
		"Gates the park/set stage-0 texture-raster resync in myDefaultCallback "
		"(cache<->device split repair). Default 1 = on. Bisect lever.");

	G("Peds", "Ped features");
	rWn("ragdollEnable", "Death ragdoll", &config->ragdollEnable,
		"Physics/render gate for the death-ragdoll replacement of the stock\n"
		"death animation. Retagged here from the Switchboard's feature list.");

	G("Main", "Lighting & ambient");
	rFn("ambientMult", "Ambient multiplier", &config->ambientMult, 0.0f, 2.0f,
		"Global multiplier applied to timecycle ambient at upload\n"
		"(GetTimecycleAmbient, pipelinecommon.cpp). 1.0 = vanilla; 0.0-2.0.");
	rFn("ambientFloor", "Absolute ambient floor", &config->ambientFloor, 0.0f, 0.2f,
		"ClampAmbientToSky tail (pipelinecommon.cpp): minimum ambient luminance.\n"
		"Default 0.03; clamped [0..0.2]. 0 = disabled.");
	rFn("pbrAmbientFloor", "PBR ped/vehicle ambient floor", &config->pbrAmbientFloor, 0.0f, 1.0f,
		"Minimum ambient luminance for ped/vehicle PBR paths (0.0-1.0, default 0.0).\n"
		"Keeps peds/vehicles visible at night without a grey veil.");
	rFn("pbrIblAmbientWeight", "IBL ambient weight", &config->pbrIblAmbientWeight, 0.0f, 2.0f,
		"Dynamic-sky hemisphere ambient weight (0 = off, 0.5 default, 1 = full).");

	G("Main", "Grass");
	rW("grassAddAmbient", "Add ambient", &config->grassAddAmbient);
	rW("grassBackfaceCull", "Backface cull", &config->backfaceCull);
	rW("grassFixPlacement", "Fix placement", &config->fixGrassPlacement);

	G("Main", "Sun & lens");
	rFn("sunCoronaIntensity", "Corona intensity", &config->sunCoronaIntensity, 0.0f, 2.0f,
		"Scales the timecycle sun corona RGB (documented range 0.0-2.0, default 0.4).");
	rFn("sunCoreIntensity", "Core intensity", &config->sunCoreIntensity, 0.0f, 2.0f,
		"Scales the sun core RGB (documented range 0.0-2.0, default 0.6).");
	rFn("sunStreakIntensity", "Streak intensity", &config->sunStreakIntensity, 0.0f, 2.0f,
		"Scales spriteBrightness / lens-flare streak brightness (0.0-2.0, default 0.5).");
	rFn("sunStreakSize", "Streak size", &config->sunStreakSize, 0.0f, 2.0f,
		"Scales spriteSize / lens-flare streak size (0.0-2.0, default 0.6).");

	G("Main", "Weather & timecycle");
	rE("lightningIlluminatesWorld", "Lightning illuminates", &config->lightningIlluminatesWorld, s_lightning, 0);
	rEn("coronaZtest", "Corona z-test", &config->coronaZtest, s_corona, -1,
		"Patches game code at 0x6FB17C through resetValues() when changed.\n"
		"-1 = leave alone, 0 = PS2 (disable z-test), 1 = PC (force z-test).\n"
		"readIniFile clamps this key to [-1..1].");
	rWn("altTimecycEnable", "Alternative timecyc (set 2)", &config->altTimecycEnable,
		"0 = vanilla CColourSet ambient/direct flow (default).\n"
		"1 = legacy exp-19 behaviour: apply weathers2.dat set-2 at t=1.0.");

	G("Main", "Dual-pass detail");
	rW("dualPassDefault", "Dual-pass default", &config->dualPassDefault);
	rW("dualPassBuilding", "Dual-pass buildings", &config->dualPassBuilding);
	rW("dualPassVehicle", "Dual-pass vehicles", &config->dualPassVehicle);
	rW("dualPassPed", "Dual-pass peds", &config->dualPassPed);
	rW("dualPassGrass", "Dual-pass grass", &config->dualPassGrass);
	rI("zwriteThreshold", "Alpha threshold", &config->zwriteThreshold, 0, 255);
	rI("zwriteThresholdGrass", "Alpha threshold (grass)", &config->zwriteThresholdGrass, 0, 255);
	rI("zwriteThresholdPed", "Alpha threshold (ped)", &config->zwriteThresholdPed, 0, 255);

	G("Main", "PS2 modulate detail");
	rW("ps2ModulateBuilding", "PS2 modulate buildings", &config->ps2ModulateBuilding);
	rW("ps2ModulateGrass", "PS2 modulate grass", &config->ps2ModulateGrass);

	G("Main", "Normal mapping detail");
	rFn("normalMapIntensity", "Normal map intensity", &config->normalMapIntensity, 0.0f, 2.0f,
		"Live when normalMapEnable=1 and the RW normal-map plugin is present.\n"
		"readIniFile does not clamp this key; 1.0 is the default.");
	rWn("normalMapPlayerOnly", "Player only", &config->normalMapPlayerOnly,
		"1 = normal maps only on the player vehicle (texdefs.cpp player scope).\n"
		"0 = all vehicles allowed by the plugin.");

	G("Main", "Normal buffer detail");
	rF("normalBufferOffset", "Normal buffer offset", &config->normalBufferOffset, 0.0f, 2.0f);
	rF("normalBufferScale", "Normal buffer scale", &config->normalBufferScale, 0.0f, 2.0f);

	// (Building reflection retagged to the new Buildings section, where the
	//  building-pipe selector and its reflection terms live together.)

	// Diagnostics section (postfxDumpDebug retagged out of the Switchboard;
	// diagLevel + debugOverlay rows live there too).
	G("Diagnostics", "Debug switches");
	rW("postfxDumpDebug", "PostFX dump debug", &config->postfxDumpDebug);
	// forwardPlusDebug: 0 = off, 1 = tile overlay, 2 = tiles + legend
	// (forwardplus.cpp FpUpdateDebugView). The compact 0/1 checkbox stays on
	// the Switchboard next to the light counters; this slider keeps mode 2
	// reachable without hand-editing the INI. Retagged here from the
	// Switchboard's rendering features.
	rIn("forwardPlusDebug", "Forward+ debug view", &config->forwardPlusDebug, 0, 2,
		"0 = off, 1 = tile overlay, 2 = tiles + legend.\n"
		"The checkbox on the Switchboard writes 0/1; this slider can also select 2.");
	// diagLevel: debug_handl.cpp's g_diagLevel verbosity latch (0=full,
	// 1=throttled, 2=silent). The Config field is live but the global the
	// logger reads is only synced at INI load, so the onEdit re-asserts it
	// for this session. saveConfigTo currently omits the key; the menu's
	// Save INI writer (MenuWriteMissingIniKeys) covers it.
	rIo("diagLevel", "Diagnostic verbosity", &config->diagLevel, 0, 2, diagLevelRowEdit,
		"0 = full logging, 1 = throttled, 2 = silent (dbglog_throttle lanes).\n"
		"Session-live via the onEdit sync into g_diagLevel; INI round-trip is\n"
		"covered by the menu's Save INI writer.");
	// debugOverlay: the F9 "F <id> | C 0x<code>" correlation readout. The
	// runtime flag is read once from the INI at overlay init and polled
	// every frame, so this row takes effect immediately; F9 still toggles.
	rW8n("debugOverlay", "Debug overlay (F9 readout)", &s_ovEnabled,
		"Top-left 'F <frameId> | C 0x<code>' correlation readout, drawn on the\n"
		"PostFX-final hook. Default 0 (OFF): the full ImGui frame cost 3-7% CPU\n"
		"every game frame — enable here or with F9 only while correlating.\n"
		"F9 toggles it too; the menu row edits the same flag.\n"
		"Saved back to [SkyGfx] debugOverlay by the menu's Save INI writer.");

	// (velocityBufferEnable retagged to the Motion blur section, where its
	// consumer lives.)

	// ================= TAB: SSAO =================
	G("SSAO", "SSAO");
	rW("ssaoEnable", "SSAO on", &config->ssaoEnable);	// mirrors Main > Effects (same var; one write per click)
	rF("ssaoRadius", "Radius", &config->ssaoRadius, 0.0f, 5.0f);
	rF("ssaoPower", "Power", &config->ssaoPower, 0.5f, 4.0f);
	rFn("ssaoKernelSize", "Kernel size", &config->ssaoKernelSize, 1.0f, 64.0f,
		"Not wired to the shader - AO spread is Radius. Kept for INI compat.");
	rIn("ssaoSampleCount", "Sample count", &config->ssaoSampleCount, 1, 64,
		"Fixed at 16 samples in the shader. Kept for INI compat.");
	rW("ssaoTemporalEnable", "Temporal", &config->ssaoTemporalEnable);
	rF("ssaoTemporalBlend", "Temporal blend", &config->ssaoTemporalBlend, 0.01f, 0.95f);
	rI("ssaoBlurPasses", "Blur passes", &config->ssaoBlurPasses, 0, 3);
	rF("ssaoBlurRadius", "Blur radius", &config->ssaoBlurRadius, 1.0f, 8.0f);
	rF("ssaoDepthThreshold", "Depth threshold", &config->ssaoDepthThreshold, 0.001f, 0.1f);

	// ================= TAB: Motion blur =================
	// Master switch lives HERE (was only on Main > Effects + PostFX): the
	// section's own pane is where a user looks to toggle the effect, and the
	// ON/OFF pill in the nav promised a switch this pane did not carry.
	// Same variable the pass gate reads (postfx.cpp skips the MotionBlur pass
	// while motionBlurEnable=0), so the toggle is live this frame.
	G("Motion blur", "Motion blur");
	rWn("motionBlurEnable", "Motion blur on", &config->motionBlurEnable,
		"Master switch of the motion-blur pass. 0 = postfx.cpp skips the pass\n"
		"entirely (PFG_MOTIONBLUR). Mirror of the Main > Effects and PostFX\n"
		"checklist rows - same variable, one write path.");
	rFn("motionBlurStrength", "Strength", &config->motionBlurStrength, 0.0f, 1.0f,
		"Overall blur intensity. 0 = no blur even with the switch on.");
	rFn("motionBlurRadial", "Radial", &config->motionBlurRadial, 0.0f, 1.0f,
		"Radial component from the screen centre (speed-line style smear).");
	rFn("motionBlurSpeedFactor", "Speed factor", &config->motionBlurSpeedFactor, 0.0f, 2.0f,
		"How much camera velocity feeds the blur.");
	rWn("motionBlurCameraAware", "Camera-aware", &config->motionBlurCameraAware,
		"Reduces blur while the camera itself is moving fast.");

	// Velocity buffer feeds the reprojection - retagged here from the
	// Switchboard's rendering features so the effect and its input live
	// in one section.
	G("Motion blur", "Velocity buffer");
	rWn("velocityBufferEnable", "Velocity buffer", &config->velocityBufferEnable,
		"Screen-space velocity target the motion-blur reprojection reads.\n"
		"Retagged here from the Switchboard's rendering features.");

	// ================= TAB: God rays =================
	G("God rays", "God rays");
	rWn("godRaysEnable", "God rays on", &config->godRaysEnable,
		"Master switch (mirror of Main > Effects / PostFX checklist).\n0 = the shaft pass is skipped this frame.");
	rF("godRaysExposure", "Exposure", &config->godRaysExposure, 0.0f, 0.05f);
	rF("godRaysDecay", "Decay", &config->godRaysDecay, 0.0f, 2.0f);
	rF("godRaysDensity", "Density", &config->godRaysDensity, 0.0f, 2.0f);
	rF("godRaysWeight", "Weight", &config->godRaysWeight, 0.0f, 5.0f);
	rI("godRaysNumSamples", "Samples", &config->godRaysNumSamples, 1, 64);

	// ================= TAB: Height fog =================
	G("Height fog", "Height fog");
	rWn("heightFogEnable", "Height fog on", &config->heightFogEnable,
		"Master switch (mirror of Main > Effects / PostFX checklist).\n0 = the fog pass is skipped this frame.");
	rFn("heightFogDensity", "Density", &config->heightFogDensity, 0.0f, 0.01f,
		"Exponential distance density. 0 = default 0.0015; clamped to 0.004 in-game so fog never becomes a wall.");
	rFn("heightFogHeightFalloff", "Height falloff", &config->heightFogHeightFalloff, 0.0f, 5.0f,
		"exp(-falloff * height). 0 = default 0.1 in-game - a literal 0 means NO height falloff (fog at every altitude), that was the blue-wash bug.");
	rF("heightFogStartHeight", "Start height", &config->heightFogStartHeight, -100.0f, 500.0f);
	rF("heightFogR", "Fog R", &config->heightFogR, 0.0f, 1.0f);
	rF("heightFogG", "Fog G", &config->heightFogG, 0.0f, 1.0f);
	rF("heightFogB", "Fog B", &config->heightFogB, 0.0f, 1.0f);
	rFn("heightFogTimecycleScale", "Timecycle scale", &config->heightFogTimecycleScale, 0.0f, 5.0f,
		"All-zero R/G/B = use the timecycle horizon colour.\nAdds timecycleScale/fogStart density (capped at 0.001) - atmosphere, not the main density.");

	// ================= TAB: SMAA =================
	G("SMAA", "SMAA");
	rWn("smaaEnable", "SMAA on", &config->smaaEnable,
		"Mirror of the switch on the main pane. SMAA has no other settings - \nquality is fixed by the implementation, so this switch is the whole feature.");

	// ================= TAB: Pipe chain =================
	G("Pipe chain", "Pipe chain");
	rWn("pipeChainEnable", "Pipe chain / SSR on", &config->pipeChainEnable,
		"Master switch (mirror of Main > Effects / PostFX checklist).\nIntensity and the reflection kill-switch below are inactive while this is 0.");
	rFn("pipeChainIntensity", "Intensity", &config->pipeChainIntensity, 0.0f, 1.0f,
		"Strength of the screen-space reflections packed by the geometry pipes\n(dull wood/road ~invisible, chrome mirror, car paint a clearcoat hint).");
	rWn("pipeChainReflEnable", "Reflection pass", &config->pipeChainReflEnable,
		"1 = the pipe-chain reflection composite runs. 0 = the reflection\n"
		"kill-switch in postfx.cpp skips it even when PipeChain is on.\n"
		"Clamped [0..1] by readIniFile.");

	// ================= TAB: Radiosity =================
	G("Radiosity", "Radiosity");
	rWn("doRadiosity", "Radiosity on", &config->doRadiosity,
		"Master switch (mirror of Main > Effects / PostFX checklist).\nThe post-processing half has its own switch (radiosityEnable, below).");
	rE("radiosity", "Radiosity type", &config->radiosity, s_rad, 0);
	rI("radiosityFilterPasses", "Filter passes", &config->radiosityFilterPasses, 0, 8);
	rI("radiosityRenderPasses", "Render passes", &config->radiosityRenderPasses, 0, 8);
	rI("radiosityIntensity", "Intensity", &config->radiosityIntensity, 0, 255);

	// ================= TAB: Colour filter =================
	G("Colour filter", "Colour filter / YCbCr");
	rWn("colorFilterEnable", "Use colour filter", &config->colorFilterEnable,
		"Pass gate (mirror of the Main pane row, same user-intent latch):\n0 = bypass the pass entirely - the first thing to try when the screen\ngoes black. Which filter runs is the Colour filter dropdown on\nRendering pipes.");
	rW8("YCbCrCorrection", "YCbCr correction", &config->bYCbCrFilter);
	rF("lumaScale", "Y scale", &config->lumaScale, 0.0f, 10.0f);
	rF("lumaOffset", "Y offset", &config->lumaOffset, -1.0f, 1.0f);
	rF("CbScale", "Cb scale", &config->cbScale, 0.0f, 10.0f);
	rF("CbOffset", "Cb offset", &config->cbOffset, -1.0f, 1.0f);
	rF("CrScale", "Cr scale", &config->crScale, 0.0f, 10.0f);
	rF("CrOffset", "Cr offset", &config->crOffset, -1.0f, 1.0f);
	rF("rgb1Mult", "RGB1 mult", &config->rgb1Mult, 0.0f, 2.0f);
	rF("rgb2Mult", "RGB2 mult", &config->rgb2Mult, 0.0f, 2.0f);

	G("Colour filter", "Colour filter crop (blur offsets)");
	rIn("blurLeft", "Crop offset left", &config->offLeft, -1000, 1000, "config->offLeft");
	rIn("blurTop", "Crop offset top", &config->offTop, -1000, 1000, "config->offTop");
	rIn("blurRight", "Crop offset right", &config->offRight, -1000, 1000, "config->offRight");
	rIn("blurBottom", "Crop offset bottom", &config->offBottom, -1000, 1000, "config->offBottom");

	// (grainFilter retagged to the Film grain section - both halves of the
	// grain feature share one pane now.)

	// ================= TAB: Tonemap =================
	// Slider bounds are the SANE ones (black-screen fix): exposure can never
	// go below 0.2 (a negative min/max window collapsed the auto-exposure
	// clamp), curve intensities stay inside +/-0.35 where PivotCurve is a
	// gentle gain ramp instead of an image-wide darkener.
	G("Tonemap", "Tonemap");
	rW("tonemapAutoExposure", "Frame-adaptive exposure", &config->tonemapAutoExposure);
	rF("tonemapAdaptSpeed", "Adapt speed", &config->tonemapAdaptSpeed, 0.01f, 1.0f);
	rF("tonemapKeyStrength", "Key strength", &config->tonemapKeyStrength, 0.0f, 1.0f);
	rFn("tonemapMinExposure", "Min exposure", &config->tonemapMinExposure, -0.5f, 0.5f,
		"Floor of the auto-exposure window; readIniFile clamps [-0.5..0.5].\nNegative values darken deliberately; Min is pinned to Max if it exceeds it.");
	rFn("tonemapMaxExposure", "Max exposure", &config->tonemapMaxExposure, 0.2f, 4.0f,
		"Ceiling of the auto-exposure window; readIniFile clamps [0.2..4.0].\nMust stay above Min exposure.");
	rFn("tonemapCurveLows", "Curve lows", &config->tonemapCurveLows, -0.35f, 0.35f,
		"Shadows (region below Curve Mid Point): negative darkens, positive lifts.\nOutside +/-0.35 the curve is forced to identity - too strong a ramp crushed the shadows.");
	rFn("tonemapCurveHighs", "Curve highs", &config->tonemapCurveHighs, -0.35f, 0.35f,
		"Highlights (region above Curve Mid Point): negative pulls down, positive boosts.\nOutside +/-0.35 the curve is forced to identity - -1.0 mapped pure white to 0.");
	rFn("tonemapCurveMid", "Curve Mid Point", &config->tonemapCurveMid, 0.0f, 1.0f,
		"Pivot where the low-curve region ends and the high-curve region begins.");
	rF("tonemapBlackLift", "Black lift", &config->tonemapBlackLift, 0.0f, 0.1f);

	// ================= TAB: IV grade =================
	G("IV grade", "GTA IV grade");
	// ivMode master switch mirrored here (Main pane + PostFX checklist carry
	// the same row): the section's pane is where the grade is tuned, so the
	// on/off lives next to its sliders. Same onEdit - the toggle is routed
	// through the mode manager, the single owner of the IV preset.
	rWn("ivMode", "IV grade on", &config->ivMode,
		"Mirror of the Main-pane switch (same onEdit): ON enters ModeManager\nMODE_GTAIV (IV pipes + colour filter applied on a config snapshot), OFF\nleaves the mode (pre-mode config restored, ivMode back to 0).");
	rFn("ivDesaturation", "Desaturation", &config->ivDesaturation, 0.0f, 2.0f,
		"Colour wash toward grey. 0 = full colour, 1 = heavily desaturated IV-grade look.");
	rFn("ivGamma", "Gamma", &config->ivGamma, 0.5f, 2.0f,
		"Midtone brightness curve of the IV grade (1.0 = neutral).");
	rF("ivSaturation", "Saturation", &config->ivSaturation, -1.0f, 2.0f);
	rF("ivCurves", "Curves", &config->ivCurves, 0.0f, 2.0f);
	rFn("ivVignetteIntensity", "Vignette intensity", &config->ivVignetteIntensity, 0.0f, 2.0f,
		"Darkening strength at the screen corners (0 = off).");
	rFn("ivVignetteRadius", "Vignette radius", &config->ivVignetteRadius, 0.0f, 2.0f,
		"How far from the centre the vignette starts.");
	rFn("ivVignetteContrast", "Vignette contrast", &config->ivVignetteContrast, 0.0f, 5.0f,
		"Hardness of the vignette falloff edge.");
	rFn("ivBloomIntensity", "Bloom", &config->ivBloomIntensity, 0.0f, 2.0f,
		"Highlight bloom strength around bright pixels.");
	rFn("ivExposure", "Exposure", &config->ivExposure, 0.0f, 3.0f,
		"Overall exposure multiplier (1.0 = neutral).");

	// ================= TAB: SSS =================
	G("SSS", "Screen-space SSS");
	rW("sssEnable", "SSS on", &config->sssEnable);		// mirrors Main > Effects
	rF("sssIntensity", "Global intensity", &config->sssIntensity, 0.0f, 1.0f);
	rF("sssVegIntensity", "Vegetation", &config->sssVegIntensity, 0.0f, 1.0f);
	rF("sssSkinIntensity", "Skin", &config->sssSkinIntensity, 0.0f, 1.0f);
	rF("sssClothIntensity", "Cloth", &config->sssClothIntensity, 0.0f, 1.0f);
	rW("sssPostProcessEnable", "Post-process blur", &config->sssPostProcessEnable);
	rF("sssPostProcessStrength", "Post-process strength", &config->sssPostProcessStrength, 0.0f, 1.0f);
	rF("sssPostProcessRadius", "Post-process radius", &config->sssPostProcessRadius, 0.0f, 20.0f);
	rF("sssPostProcessThreshold", "Post-process threshold", &config->sssPostProcessThreshold, 0.0f, 1.0f);
	rF("sssAmbientBoost", "Ambient boost", &config->sssAmbientBoost, 0.0f, 2.0f);

	G("SSS", "Material enhancement (skin/hair/vegetation)");
	rW("skinEnhanceEnable", "Skin enhance", &config->skinEnhanceEnable);
	rF("skinWrapFactor", "Skin wrap factor", &config->skinWrapFactor, 0.0f, 1.0f);
	rF("skinSpecularPower", "Skin specular power", &config->skinSpecularPower, 0.0f, 100.0f);
	rF("skinSpecularStrength", "Skin specular strength", &config->skinSpecularStrength, 0.0f, 2.0f);
	rF("skinSSSStrength", "Skin SSS strength", &config->skinSSSStrength, 0.0f, 1.0f);
	rW("hairEnhanceEnable", "Hair enhance", &config->hairEnhanceEnable);
	rF("hairAnisotropicPower", "Hair aniso power", &config->hairAnisotropicPower, 0.0f, 128.0f);
	rF("hairAnisotropicStrength", "Hair aniso strength", &config->hairAnisotropicStrength, 0.0f, 2.0f);
	rF("hairSSSStrength", "Hair SSS strength", &config->hairSSSStrength, 0.0f, 1.0f);
	rW("vegetationEnhanceEnable", "Vegetation enhance", &config->vegetationEnhanceEnable);
	rF("vegetationSSSStrength", "Vegetation SSS strength", &config->vegetationSSSStrength, 0.0f, 1.0f);
	rF("vegetationAmbientBoost", "Vegetation ambient boost", &config->vegetationAmbientBoost, 0.0f, 3.0f);

	// ================= TAB: VCS trails =================
	G("VCS trails", "VCS trails");
	rWn("vcsTrails", "VCS trails on", &config->vcsTrails,
		"Master switch (mirror of Main > Effects / PostFX checklist).\n0 = no trail draws at all.");
	rI("trailsLimit", "Trail limit", &config->trailsLimit, 0, 255);
	rI("trailsIntensity", "Trail intensity", &config->trailsIntensity, 0, 255);
	rI("trailsResolution", "Trail resolution", &config->trailsResolution, 1, 4);

	// ================= TAB: Water =================
	G("Water", "Water style & quality");
	rE("waterStyle", "Water style", &config->waterStyle, s_waterSty, 0);
	rE("waterQuality", "Water quality", &config->waterQuality, s_waterQ, 0);
	rWn("waterUseTimecycle", "Colour from timecycle", &config->waterUseTimecycle,
		"1 = water colour/alpha from timecyc, 0 = the Deep/Shallow keys below win.");

	G("Water", "Water appearance");
	rW("waterParallaxEnable", "Parallax", &config->waterParallaxEnable);
	rF("waterParallaxScale", "Parallax scale", &config->waterParallaxScale, 0.0f, 4.0f);
	rF("waterNormalStrength", "Normal strength", &config->waterNormalStrength, 0.0f, 4.0f);
	rF("waterFresnelPower", "Fresnel power", &config->waterFresnelPower, 0.0f, 12.0f);
	rF("waterSpecularPower", "Specular power", &config->waterSpecularPower, 1.0f, 1024.0f);
	rF("waterSpecularIntensity", "Specular intensity", &config->waterSpecularIntensity, 0.0f, 4.0f);
	rF("waterUnderwaterFog", "Underwater fog", &config->waterUnderwaterFog, 0.0f, 4.0f);
	rF("waterFoamThreshold", "Foam threshold", &config->waterFoamThreshold, 0.0f, 1.0f);
	rF("waterFoamSoftness", "Foam softness", &config->waterFoamSoftness, 0.0f, 1.0f);
	rF("waterShallowR", "Shallow R", &config->waterShallowR, 0.0f, 1.0f);
	rF("waterShallowG", "Shallow G", &config->waterShallowG, 0.0f, 1.0f);
	rF("waterShallowB", "Shallow B", &config->waterShallowB, 0.0f, 1.0f);
	rF("waterDeepR", "Deep R", &config->waterDeepR, 0.0f, 1.0f);
	rF("waterDeepG", "Deep G", &config->waterDeepG, 0.0f, 1.0f);
	rF("waterDeepB", "Deep B", &config->waterDeepB, 0.0f, 1.0f);

	// --- Water feature switches: every water feature one row, so the water
	// pipe can be treated one piece at a time. (The two formerly greyed W7-A
	// / W7-B rows are live now — Config fields + parse/save landed; the staged
	// diff they came from is .serena/agent-reports/dbg-menu-categories.md.)
	G("Water", "Water feature switches");
	rWn("waterParallaxEnable", "Water pipe (master)", &config->waterParallaxEnable,
		"Master switch of the whole custom water pipe (main.cpp's draw gate).\n0 = the game's original water draw.");
	rWn("mirrorDistortionFix", "Mirror distortion fix", &config->mirrorDistortionFix,
		"CMirrors composite: overdraws the mirror blit with a per-pixel\n"
		"projective fetch (FusionFix mirrorPS1.asm port, mirrorPass.cpp).\n"
		"0 = the game's fixed-UV fullscreen blit (vanilla).");
	rIn("reflectionMsaaLevel", "Reflection MSAA level", &config->reflectionMsaaLevel, 0, 8,
		"0/2/4/8. RW3.6 rasters have no multisample API, so the level\n"
		"SUPERsamples the vehicle env-map raster (envMapSize x level, cap 2048).\n"
		"Values outside {0,2,4,8} behave as 0 (off).");
	rFn("waterUnderwaterFog", "Underwater fog strength", &config->waterUnderwaterFog, 0.0f, 4.0f,
		"Depth/blue wash when the camera is below the surface (postfx_underwater.cpp).\n0 = no underwater fog at all.");
	// (waterFoamThreshold / waterFoamSoftness deduped: they live in the
	// "Water appearance" group above, next to the other water shading knobs.)
	rWn("waterShoreFoam", "Shorelines / foam (W7-A)", &config->waterShoreFoam,
		"W7-A smooth-shoreline coverage + multi-octave foam band\n"
		"(LibertyRecomp/Fusion port in Water_Parallax.hlsl).\n"
		"0 = ranges pushed out of reach in waterPipe.cpp (no shader edit).");
	rWn("waterBodyClassify", "Body classification gate (classifyGate)", &config->waterBodyClassify,
		"W7-B per-vertex bodyType/edgeBlend channel (VS c10.x + PS c14).\n"
		"0 = WbClassifyFlush skipped, every WaterBodyContract scale pinned\n"
		"to 1.0 = exact vanilla water shading.");
	rWn("waterFresnelSplit", "Fresnel-split composite", &g_waterFresnelSplit,
		"waterPipe.cpp c11.w: 1 = reflection AND refraction live at every view\n"
		"direction (default), 0 = legacy lerp(scene, surf, bodyOpacity) composite.\n"
		"Live global. The menu's Save INI writer now covers this key; the old\n"
		"saveConfigTo omitted it.");

	// W7-C: replaced the dead waterReflectionFarClip row (key deleted — zero
	// consumers) with the CMirrors composite fix. (The mirrorDistortionFix
	// row lives in the "Water feature switches" group above — deduped here.)
	rF("waterReflectionStrength", "Reflection strength", &config->waterReflectionStrength, 0.0f, 1.0f);
	// waterReflectionMix: coverage-gap row — the key was parsed and saved
	// (readIniFile + saveConfigTo) but had no menu row, so it was only
	// reachable by hand-editing the INI. waterPipe.cpp reads it live.
	rFn("waterReflectionMix", "Reflection mix", &config->waterReflectionMix, 0.0f, 1.0f,
		"Reflection/colour mix of the custom water pipe (waterPipe.cpp).\n"
		"0 = colour only, 1 = full mirror. Default 0.7.");
	rF("waterTileScale", "Tile scale", &config->waterTileScale, 0.1f, 8.0f);
	rF("waterShoreFade", "Shore fade", &config->waterShoreFade, 0.0f, 20.0f);
	rF("waterTranslucency", "Translucency", &config->waterTranslucency, 0.0f, 2.0f);

	// ================= TAB: PostFX — systematic post-FX checklist =================
	// One row per pass, every row individually toggleable, grouped by effect
	// so a feature can be checked off / switched off one at a time (the
	// full-screen-black hunt). Master switches first, then each effect's own
	// detail rows. The per-effect EFFECTS sections (SSAO / Tonemap /
	// IV grade / ...) mirror the SAME Config fields — one home per variable,
	// two places to reach it, exactly the Switchboard <-> section pattern.
	G("PostFX", "PostFX passes - switch ONE off at a time");
	rW("ssaoEnable", "SSAO", &config->ssaoEnable);
	rWn("ssaoTemporalEnable", "SSAO overhaul (quarter-res temporal)", &config->ssaoTemporalEnable,
		"The overhaul the SSAO lane shipped: quarter-res AO + temporal\n"
		"reprojection + blur. 0 = the pre-overhaul single-pass SSAO path.");
	rW("smaaEnable", "SMAA", &config->smaaEnable);
	rWn("fxaaEnable", "FXAA3", &config->fxaaEnable,
		"Late AA slot - SMAA wins when both are enabled (FXAA is suppressed).");
	rW("motionBlurEnable", "Motion blur", &config->motionBlurEnable);
	rW("tonemapAutoExposure", "Adaptive tonemapper", &config->tonemapAutoExposure);
	rW("bloomEnable", "Bloom", &config->bloomEnable);
	rW("sunShaftsEnable", "Sun shafts", &config->sunShaftsEnable);
	rW("dofEnable", "Depth of field", &config->dofEnable);
	rW("grainEnable", "Film grain", &config->grainEnable);
	A("ivMode", "IV grade", RK_BOOL, &config->ivMode, 0, 0, NULL, 0, ivModeRowEdit,
		"Mirror of the Main-pane switch (same onEdit): ON enters ModeManager MODE_GTAIV\n"
		"(IV pipes + colour filter applied on a config snapshot), OFF leaves the mode\n"
		"(pre-mode config restored). Grade sliders are carried through the snapshot.");

	// Every remaining post pass, so nothing in the chain is un-reachable.
	G("PostFX", "Other post passes (detail on their own tabs)");
	rWn("godRaysEnable", "God rays", &config->godRaysEnable, "Detail sliders: God rays tab.");
	rWn("heightFogEnable", "Height fog", &config->heightFogEnable, "Detail sliders: Height fog tab.");
	rWn("sssEnable", "Screen-space SSS", &config->sssEnable, "Detail sliders: SSS tab.");
	rWn("sssPostProcessEnable", "SSS post-process blur", &config->sssPostProcessEnable,
		"Blurs the whole scene with edge preservation - low strength only.");
	rWn("doRadiosity", "Radiosity", &config->doRadiosity, "Detail sliders: Radiosity tab.");
	rWn("radiosityEnable", "Radiosity (post-processing)", &config->radiosityEnable,
		"The shader radiosity pass (radiosity type on the Radiosity tab).");
	A("colorFilterEnable", "Colour filter", RK_BOOL, &config->colorFilterEnable, 0, 0, NULL, 0, cfEnableRowIntent,
		"Mirror of the Main-pane gate (same intent latch). 0 = bypass the pass\n"
		"entirely - the first thing to try when the screen goes black.");
	rWn("pipeChainEnable", "Pipe chain / SSR", &config->pipeChainEnable,
		"BRDF-classified screen-space reflections packed by the geometry pipes.");
	rWn("vcsTrails", "VCS trails", &config->vcsTrails, "Detail sliders: VCS trails tab.");
	rWn("waterParallaxEnable", "Water (post-water composite)", &config->waterParallaxEnable,
		"Master switch of the whole water pipe: 0 = the game's original water draw.");

	G("PostFX", "Anti-aliasing (SMAA / FXAA3)");
	rWn("smaaEnable", "SMAA", &config->smaaEnable,
		"Mirror of the master switch above. SMAA has no other settings - quality\n"
		"is fixed by the implementation, so this switch is the whole feature.");
	rWn("fxaaEnable", "FXAA3", &config->fxaaEnable,
		"Mirror of the master switch above.");

	G("PostFX", "Adaptive tonemapper");
	rW("tonemapAutoExposure", "Frame-adaptive exposure", &config->tonemapAutoExposure);
	rF("tonemapAdaptSpeed", "Adapt speed", &config->tonemapAdaptSpeed, 0.01f, 1.0f);
	rF("tonemapKeyStrength", "Key strength", &config->tonemapKeyStrength, 0.0f, 1.0f);
	rFn("tonemapMinExposure", "Min exposure", &config->tonemapMinExposure, -0.5f, 0.5f,
		"Floor of the auto-exposure window; readIniFile clamps [-0.5..0.5].\nNegative values darken deliberately; Min is pinned to Max if it exceeds it.");
	rFn("tonemapMaxExposure", "Max exposure", &config->tonemapMaxExposure, 0.2f, 4.0f,
		"Ceiling of the auto-exposure window; readIniFile clamps [0.2..4.0].\nMust stay above Min exposure.");

	G("PostFX", "Tonemap operator (LUT)");
	rEn("tonemapOperator", "Operator", &config->tonemapOperator, s_tonemapOps, 0,
		"0 = filmic (default), 1 = ACES 1.3, 2 = ACES 2.0, 3 = AgX, 4 = Frostbite, 5 = GT7.\n"
		"Swap only when > 0 - the stock Hable/Uncharted2 filmic curve stays put at 0.\n"
		"Applied by postfx_colourfilter.cpp (FxTonemapLut pass).");
	rFn("tonemapCurveLows", "Curve lows", &config->tonemapCurveLows, -0.35f, 0.35f,
		"Shadows (region below Curve Mid Point): negative darkens, positive lifts.");
	rFn("tonemapCurveHighs", "Curve highs", &config->tonemapCurveHighs, -0.35f, 0.35f,
		"Highlights (region above Curve Mid Point). Outside +/-0.35 the curve is\nforced to identity - -1.0 mapped pure white to 0.");
	rFn("tonemapCurveMid", "Curve mid point", &config->tonemapCurveMid, 0.0f, 1.0f,
		"Pivot where the low-curve region ends and the high-curve region begins.");
	rF("tonemapBlackLift", "Black lift", &config->tonemapBlackLift, 0.0f, 0.1f);

	// Bloom / Sun shafts / DOF / Film grain retagged out of the PostFX
	// checklist into their own Effects sections (the checklist keeps the
	// master switches; the sliders live with their effect).
	G("Bloom", "Bloom");
	rW("bloomEnable", "Bloom on", &config->bloomEnable);
	rEn("bloomQuality", "Quality", &config->bloomQuality, s_bloomQ, 0,
		"0-3 = 2..6 mips in the bloom down/up chain.");
	rF("bloomThreshold", "Threshold", &config->bloomThreshold, 0.0f, 2.0f);
	rF("bloomIntensity", "Intensity", &config->bloomIntensity, 0.0f, 3.0f);

	G("Sun shafts", "Sun shafts");
	rW("sunShaftsEnable", "Sun shafts on", &config->sunShaftsEnable);
	rF("sunShaftsDensity", "Density", &config->sunShaftsDensity, 0.0f, 1.0f);
	rF("sunShaftsDecay", "Decay", &config->sunShaftsDecay, 0.0f, 1.0f);

	G("DOF", "Depth of field");
	rW("dofEnable", "DOF on", &config->dofEnable);
	rF("dofFocusDistance", "Focus distance", &config->dofFocusDistance, 0.0f, 100.0f);
	rF("dofFocusRange", "Focus range", &config->dofFocusRange, 0.1f, 50.0f);
	rF("dofBokehRadius", "Bokeh radius", &config->dofBokehRadius, 1.0f, 10.0f);
	rF("dofMaxBlur", "Max blur", &config->dofMaxBlur, 0.0f, 1.0f);

	// Film grain family: the new FX grain pass rows moved here from the
	// PostFX checklist; the legacy grainFilter kernel combo joins them from
	// the Colour filter tab. One section, both halves of the feature.
	G("Film grain", "Film grain (FX pass)");
	rW("grainEnable", "Grain filter", &config->grainEnable);
	rFn("grainIntensity", "Intensity", &config->grainIntensity, 0.0f, 1.0f,
		"0 = off. The new FX grain pass (postfx_newfx.cpp) - separate from the\n"
		"legacy grainFilter type combo, which only picks the grain kernel.");

	// Legacy grain kernel type (retagged from the Colour filter tab so both
	// halves of the grain feature live in one section).
	G("Film grain", "Grain kernel (legacy type)");
	rE("grainFilter", "Grain filter", &config->grainFilter, s_ps2pc, 0);

	G("PostFX", "Chromatic aberration");
	rFn("caIntensity", "Intensity", &config->caIntensity, 0.0f, 1.0f,
		"0 = off. RGB channel offset toward the screen edges.");

	G("PostFX", "Vignette");
	rFn("vignetteIntensity", "Intensity", &config->vignetteIntensity, 0.0f, 1.0f,
		"0 = off. Binds the legacy Config vignetteIntensity field (skygfx.h) -\nthe same member postfx.cpp's FxVignette pass reads.");
	rFn("vignetteRadius", "Radius", &config->vignetteRadius, 0.0f, 2.0f,
		"Not wired - FxVignette hard-codes 0.85. Kept for INI compat.\n"
		"ROUND-TRIP GAP: readIniFile never parses this key and saveConfigTo\n"
		"never writes it, so an edit here is session-only (reverts on reload).");
	rFn("vignetteContrast", "Contrast", &config->vignetteContrast, 0.0f, 5.0f,
		"Not wired - FxVignette hard-codes 0.45. Kept for INI compat.\n"
		"ROUND-TRIP GAP: readIniFile never parses this key and saveConfigTo\n"
		"never writes it, so an edit here is session-only (reverts on reload).");

	G("PostFX", "SSAO detail");
	rF("ssaoRadius", "Radius", &config->ssaoRadius, 0.0f, 5.0f);
	rF("ssaoPower", "Power", &config->ssaoPower, 0.5f, 4.0f);
	rFn("ssaoKernelSize", "Kernel size", &config->ssaoKernelSize, 1.0f, 64.0f,
		"Not wired to the shader - AO spread is Radius. Kept for INI compat.");
	rIn("ssaoSampleCount", "Sample count", &config->ssaoSampleCount, 1, 64,
		"Fixed at 16 samples in the shader. Kept for INI compat.");
	rF("ssaoTemporalBlend", "Temporal blend", &config->ssaoTemporalBlend, 0.01f, 0.95f);
	rI("ssaoBlurPasses", "Blur passes", &config->ssaoBlurPasses, 0, 3);
	rF("ssaoBlurRadius", "Blur radius", &config->ssaoBlurRadius, 1.0f, 8.0f);
	rF("ssaoDepthThreshold", "Depth threshold", &config->ssaoDepthThreshold, 0.001f, 0.1f);

	G("PostFX", "Motion blur detail");
	rF("motionBlurStrength", "Strength", &config->motionBlurStrength, 0.0f, 1.0f);
	rF("motionBlurRadial", "Radial", &config->motionBlurRadial, 0.0f, 1.0f);
	rF("motionBlurSpeedFactor", "Speed factor", &config->motionBlurSpeedFactor, 0.0f, 2.0f);
	rW("motionBlurCameraAware", "Camera-aware", &config->motionBlurCameraAware);

	// ================= TAB: Vegetation =================
	// Everything grass/tree in one category: the W8-3 instancing gate, the
	// W8-2 tree wind / alpha lane, the vegetation SSS enhancement and the
	// grass shading switches (mirrors of the Main pane's Grass group).
	G("Vegetation", "Vegetation features");
	rWn("grassInstancing", "Grass instancing (batched plant islands)", &config->grassInstancing,
		"W8-3: one RwIm3DTransform per texture instead of a per-plant RpAtomicRender.\n"
		"Applied live through CGrassInstancer::SetEnabled() (readIniFile + InjectDelayedPatches).\n"
		"Default 0 (OFF): the gate is closed and the game's own renderer runs unchanged.");
	rWn("treeWind", "Tree wind (game hook 0x534E90)", &config->treeWind,
		"1 = install the ModifyMatrixForTreeInWind port (TreeWind_OnEntity),\n"
		"0 = the stock routine. STARTUP-ONLY: the hook install in InjectDelayedPatches\n"
		"reads the flag once, so changing it here needs a restart. Default 0 (OFF)\n"
		"for the current debugging round. The PBR/IV CPU tilt (TreeWind_ApplyToTransform)\n"
		"is independent of this flag.");
	rEo("treeFx", "Tree alpha profile", &config->treeFx, s_treeFx, 0, treeFxRowEdit,
		"0 Off, 1 PC, 2 PC+, 3 Console - FusionFix TreeFxText values, forwarded\n"
		"to TreeAlpha_SetProfile immediately. PC/PC+ move the CPU alpha-test ref\n"
		"in every pipe; Console also binds TreeAlpha.hlsl.");

	G("Vegetation", "Grass intensity variance");
	// W8-1 per-plant intensity variance is always-on (no Config field yet):
	// greyed so the gap is visible instead of silently missing. The exact
	// skygfx.h / main.cpp / PC_GrassRenderer / PC_GrassInstancer diff is staged
	// in .serena/agent-reports/dbg-menu-categories.md.
	rN("grassIntensityVar", "Per-plant intensity variance",
		"STAGED - no live variable yet: W8-1 applies intensity variance per plant\n"
		"(final_I = I + VarI*rand01()) with a vanilla guard and no INI toggle.\n"
		"Exact field + parse/save + render diff: .serena/agent-reports/dbg-menu-categories.md");

	G("Vegetation", "Grass shading");
	rW("grassAddAmbient", "Add ambient", &config->grassAddAmbient);
	rW("grassBackfaceCull", "Backface cull", &config->backfaceCull);
	rW("grassFixPlacement", "Fix placement", &config->fixGrassPlacement);
	rW("dualPassGrass", "Dual-pass grass", &config->dualPassGrass);
	rW("ps2ModulateGrass", "PS2 modulate grass", &config->ps2ModulateGrass);
	rI("zwriteThresholdGrass", "Alpha threshold (grass)", &config->zwriteThresholdGrass, 0, 255);

	G("Vegetation", "Vegetation enhancement (SSS / wrap)");
	rW("vegetationEnhanceEnable", "Enhance", &config->vegetationEnhanceEnable);
	rF("vegetationSSSStrength", "SSS strength", &config->vegetationSSSStrength, 0.0f, 1.0f);
	rF("vegetationAmbientBoost", "Ambient boost", &config->vegetationAmbientBoost, 0.0f, 3.0f);

	// ================= TAB: Buildings =================
	// The building-pipe selector, one click from Vehicle in the WORLD
	// category. The Switchboard's "Rendering pipes" group stays the
	// pipeline-mapping home; this section is the per-domain home the nav
	// actually suggests. Same raw-edit contract as the Main row:
	// PipeRowEdit re-syncs ivMode ONLY when the edit crosses the IV family
	// boundary (grade follows the pipes).
	G("Buildings", "Building pipe");
	rEo("buildingPipe", "Building pipe", &config->buildingPipe, s_buildNames, 0, PipeRowEdit,
		"Mirror of the Switchboard row (same variable, same ivMode family sync).\n"
		"0 PS2, 1 Xbox, 2 GTAIV, 3 PBR - the enum readIniFile's StrAssoc maps.");
	G("Buildings", "Building reflection");
	rWn("bldEnvReflect", "Env-mapped materials", &config->bldEnvReflect,
		"Restores the additive Envcolor = 192/128 * shininess reflection that the GTA IV / PS2\n"
		"/ Xbox building cbs run for materials with the envmap flag - the PBR cb had no\n"
		"branch for them, so Building=PBR rendered those materials flat.\n"
		"0 = legacy PBR (no building reflections at all).");
	rFn("bldSkyReflect", "Sky reflection strength", &config->bldSkyReflect, 0.0f, 1.0f,
		"Superset term the GTA IV pipe does NOT have: Fresnel-weighted reflection of the\n"
		"IBL sky capture for materials without their own env map. Energy-consistent\n"
		"(F*env + (1-F)*body, weight forced to 0 when the capture is empty). 0 = off.");

	// ================= TAB: Vehicle / Chrome =================
	// Vehicle-pipe selector first: the same discoverability fix as the
	// Buildings section - the pipe dropdown lives at the top of the vehicle
	// pane instead of only inside the Switchboard's pipeline mapping.
	G("Vehicle", "Vehicle pipe");
	rEo("vehiclePipe", "Vehicle pipe", &config->vehiclePipe, s_vehNames, 0, PipeRowEdit,
		"Mirror of the Switchboard row (same variable, same ivMode family sync).\n"
		"0 PS2, 1 PC, 2 Xbox, 3 Specular, 4 Mobile, 5 Neo, 6 Leeds, 7 VCS,\n"
		"8 Env, 9 GTAIV, 10 Modern - the enum readIniFile's StrAssoc maps.");
	G("Vehicle", "Chrome BRDF");
	rF("chromeF0", "Chrome F0", &config->chromeF0, 0.0f, 1.0f);
	rF("chromeGloss", "Chrome gloss", &config->chromeGloss, 0.0f, 1.0f);
	rF("chromeMetallic", "Chrome metallic", &config->chromeMetallic, 0.0f, 1.0f);
	rF("chromeEnvBoost", "Chrome env boost", &config->chromeEnvBoost, 0.0f, 4.0f);
	rF("chromeClearcoat", "Chrome clearcoat", &config->chromeClearcoat, 0.0f, 1.0f);
	rW("vehAutoChrome", "Auto chrome (part names)", &config->vehAutoChrome);
	rFn("vehChromeEnvThreshold", "Chrome env threshold", &config->vehChromeEnvThreshold, 0.0f, 10.0f,
		"0 = disabled; promotes materials whose raw env-map shininess is >= this.");

	G("Vehicle", "Env map / reflections");
	rIn("envMapSize", "Env map size", &config->envMapSize, 4, 2048,
		"readIniFile rounds the key up to the next power of two; the env-map\nraster is capped at 2048. ChangeEnvMapSize() applies the same snap.");
	rW("envMapUseLODs", "Env map use LODs", &config->envMapUseLODs);
	rF("envMapFarClipMult", "Env far clip mult", &config->envMapFarClipMult, 0.1f, 10.0f);
	rIn("reflectionMsaaLevel", "Reflection MSAA (supersample)", &config->reflectionMsaaLevel, 0, 8,
		"0/2/4/8. RW3.6 rasters have no multisample API, so the level\n"
		"SUPERsamples the vehicle env-map raster (envMapSize x level, cap 2048).\n"
		"Values outside {0,2,4,8} behave as 0 (off).");
	rF("envPower", "Env power", &config->envPower, 1.0f, 128.0f);
	rF("envFresnel", "Env fresnel", &config->envFresnel, 0.0f, 0.4f);
	rF("envShininessMult", "Env shininess mult", &config->envShininessMult, 0.0f, 10.0f);
	rF("envSpecularityMult", "Env specularity mult", &config->envSpecularityMult, 0.0f, 10.0f);
	rF("vehEnvIntensity", "Env reflection intensity", &config->vehEnvIntensity, 0.0f, 0.4f);

	// Modern/Env reflection-model terms — one control per term ported from the
	// other car pipes; rationale lives in skygfx.h next to each field and in
	// the "reflection register contract" note in VehiclePBR_Modern.hlsl
	// (consumed through PS c44.z/w and c20.y/z/w). The three switches are
	// 0 = exact legacy Modern behaviour, so the old look is always one click
	// away while testing.
	rW("vehEnvFallback", "Env validity fallback (no black lerp)", &config->vehEnvFallback);
	rW("vehEnvStrengthMode", "Energy-consistent strength (F-coverage)", &config->vehEnvStrengthMode);
	rW("vehEnvMask", "Reflection mask (CarReflectionMask)", &config->vehEnvMask);
	rW("vehWetEnv", "Wet-road reflections", &config->vehWetEnv);
	rFn("vehEnvGlint", "Mobile sun glint strength", &config->vehEnvGlint, 0.0f, 1.0f,
		"Ported from main_mobileVehicle: pow(dot(reflectV, sunDir), 10) * 2 * sunColor,\n"
		"texture-independent so it survives a dead reflectionTex. 0 = off (Modern already\n"
		"has the GGX sun lobe and envGlint); also gated by layer bitmask bit2.");

	G("Vehicle", "PBR vehicle");
	rIn("vehPBRLayers", "PBR layer bitmask", &config->vehPBRLayers, 0, 255,
		"bit0 diffuse, 1 env, 2 sun/spec, 3 rim, 4 IBL, 5 sky tint, 6 clearcoat, 7 normal buffer. 255 = all on.");
	rF("pbrIblAmbientWeight", "IBL ambient weight", &config->pbrIblAmbientWeight, 0.0f, 2.0f);
	rF("pbrAmbientFloor", "Ambient floor", &config->pbrAmbientFloor, 0.0f, 1.0f);
	rF("vehPrelightFallback", "Non-prelit prelight floor", &config->vehPrelightFallback, 0.0f, 1.0f);

	G("Vehicle", "Legacy car-pipe shading");
	rF("leedsShininessMult", "Leeds shininess", &config->leedsShininessMult, 0.0f, 10.0f);
	rF("neoShininessMult", "Neo shininess", &config->neoShininessMult, 0.0f, 10.0f);
	rF("neoSpecularityMult", "Neo specularity", &config->neoSpecularityMult, 0.0f, 10.0f);

	// ================= TAB: Profiler =================
	// The two switches live here (plain toggles, no mode semantics); the live
	// readout below them is drawn by DrawProfilerTab().
	G("Profiler", "Profiler switches");
	A("profilerEnable", "Profile", RK_BOOL, &config->profilerEnable, 0, 0, NULL, 0, profilerEnableRowEdit,
		"Profiler master switch (default ON this testing round).\n"
		"Collects PF_SCOPE CPU (QPC) timings, frame time, draw/vert/tri and\n"
		"shader-bind counters, writes a ~1/s summary to skygfx_dbg.log and feeds\n"
		"the table below. Disabled costs one volatile read per scope.");
	A("profilerGpuTimers", "GPU timers (D3D9)", RK_BOOL, &config->profilerGpuTimers, 0, 0, NULL, 0, profilerGpuRowEdit,
		"Brackets every scope with D3DQUERYTYPE_TIMESTAMP + TIMESTAMPDISJOINT queries.\n"
		"Off by default: timestamp queries are not supported by every driver and\n"
		"each span costs driver round-trips. The profiler abandons them for the\n"
		"session and falls back to CPU timings if CreateQuery fails.");

	// ================= TAB: Utility =================
	G("Utility", "Utility noise texture");
	A("NoiseQuality", "Noise quality", RK_NOISEQ, NULL, 0, 0, s_noise, 0, NULL,
		"[General] NoiseQuality -> tile resolution 0=32, 1=128, 2=512, >=3=1024.\nStatic in main.cpp: written through the menu accessors, then the tile is\nregenerated (same path refreshIni takes on F11).");

	G("Utility", "Hotkeys");
	rIn("keySwitch", "Menu hotkey (scan code)", &config->keys[0], 0, 512,
		"Hex in the INI (readhex). Applied live - the key poll reads config->keys.");
	rIn("keyReload", "Reload hotkey (scan code)", &config->keys[1], 0, 512,
		"Hex in the INI (readhex). Applied live - the key poll reads config->keys.");

	G("Utility", "Startup-only keys (restart required)");
	rS8("disableClouds", "disableClouds", &disableClouds);
	rS8("disableGamma", "disableGamma", &disableGamma);
	rS8("fixPcCarLight", "fixPcCarLight", &fixPcCarLight);
	rSn("privateHooks", "privateHooks", &privateHooks,
		"Startup-only hook gate; readIniFile overwrites it on reload but the\n"
		"InjectDelayedPatches consumer only runs during process init.");
	rSn("forceWindShader", "forceWindShader", &forceWindShader,
		"Only ever set true by readIniFile (==1); never cleared by a reload.");
	rS("transparentLockon", "transparentLockon", &transparentLockon);
	rS("fixShadows", "fixShadows", &fixShadows);
	rSn("explicitBuildingPipe", "explicitBuildingPipe", &explicitBuildingPipe_tmp,
		"Copied to explicitBuildingPipe once at startup (InjectDelayedPatches).\nShown greyed: the live variable only moves on restart.");
	A("forwardPlusFallback", "Forward+ empty-tile fallback", RK_INIINT, NULL, 0, 0, NULL, 0, NULL,
		"Read ONCE at startup by forwardplus.cpp (FpFallbackEnabled) from the\n"
		"active INI; there is no live Config field or runtime global to bind.\n"
		"1 = legacy 7-light path when the tile list is empty (default),\n"
		"0 = always bind the cluster path. Restart required.");

	G("Utility", "Read by readIniFile but absent from skygfx.ini");
	rN("ps2grassFiles", "ps2grassFiles",
		"PARSER DISABLED: the read line is commented out in readIniFile.\nListed so it is not silently invisible.");

	#undef G
	#undef A
	#undef rW
	#undef rW8
	#undef rW8n
	#undef rI
	#undef rF
	#undef rE
	#undef rS
	#undef rS8
	#undef rN
	#undef rWn
	#undef rIn
	#undef rFn
	#undef rEn
	#undef rSn
	#undef rIo
	#undef rEo
	return v;
}

static std::vector<SettingRow> &
GetSettingRows(void)
{
	static std::vector<SettingRow> rows;
	static Config *rowsCfg = NULL;
	if(!config)
		return rows;
	if(rows.empty() || rowsCfg != config){	// Config selector moved -> re-resolve pointers
		rows = BuildSettingRows();
		rowsCfg = config;
	}
	return rows;
}

// ============================================================
// COVERAGE MATRIX — built from src/skygfx.h `struct Config` and the
// authoritative parser at src/core/main.cpp readIniFile (2026-10-24).
//
// Total Config fields: 436. Live/render-relevant: 235.
// The "All Settings" tab (GetCoverageRows) shows every live key exactly
// once, grouped by subsystem; the per-subsystem tabs above remain the
// editable homes. Startup-only globals with no Config field are included
// where a live variable exists (cloudRenderFix, waterFresnelSplit,
// forwardPlusFallback is shown read-only from the active INI).
//
// INTENTIONALLY HIDDEN (201 fields, none of them read by a render/frame
// path outside skygfx.h):
//  * version, tagsBuildingPipe — internal ABI / mirror of buildingPipe.
//  * debugMenuOpen — the ImGui window state itself, not a render setting.
//  * _reserved_smaaPreset, _reserved_smaaPredication, _reserved_smaaTemporal
//    — padding only; SMAA has no user-tunable preset any more.
//  * The whole Expanded Weather/Timecycle block, skyZenithR through
//    lensChromaticAberrationCubeCoeff — 155 fields. Grep finds no reader
//    outside skygfx.h; readIniFile has no keys for them. The three
//    vignette fields inside that block (vignetteIntensity/Radius/Contrast)
//    ARE live and are exposed via the FxVignette rows.
//  * currentWeatherType, weatherTransition, weatherCycleEnabled,
//    timecycleOverrideHour, timecycleOverrideEnabled — 5 fields, no reader.
//  * lensArtefactsInten, lensArtefactsIntenMinExp, lensArtefactsIntenMaxExp
//    — 3 fields immediately after the block, also zero readers.
//  * The dead JuniorDjjr unified-pipeline block, unifiedEnable through
//    unifiedEnableGamma — 32 fields. Zero readers: their only consumer,
//    UploadUnifiedConstants (never called, unconditional early return), was
//    removed 2026-11-01 (source-inventory §2). The fields themselves live in
//    skygfx.h (other lane) and readIniFile has no keys for them.
// This comment is the required explicit hidden-with-reason list: if one of
// these gains a reader, move it into BuildSettingRows instead of leaving it
// invisible.
// ============================================================

// Per-row reset default. Values mirror readIniFile's literal defaults
// (or the fresh-install INI / documented default where the parser uses
// `c->field` as its fallback). Presets are NOT re-applied by a reset.
struct KeyDefault { const char *key; float value; };
static const KeyDefault kKeyDefaults[] = {
	{ "pipeline", 0 }, { "pipelineOverride", -1 }, { "buildingPipe", 3 }, { "vehiclePipe", 10 },
	{ "waterStyle", 0 }, { "waterQuality", 3 },
	// readIniFile's missing-key default is PC (2). Under PBR the filter is
	// forced to Modern (8) every frame and the row is marked overridden, so
	// this reset target is only observable on non-PBR pipes.
	{ "colorFilter", 2 }, { "ivMode", 0 },
	{ "gamePreset", -1 }, { "colorFilterEnable", 1 },
	{ "ssaoEnable", 0 }, { "smaaEnable", 0 }, { "ssaoRadius", 1.0f }, { "ssaoPower", 2.0f },
	{ "ssaoKernelSize", 16 }, { "ssaoSampleCount", 16 }, { "ssaoTemporalEnable", 1 },
	{ "ssaoTemporalBlend", 0.1f }, { "ssaoBlurPasses", 2 }, { "ssaoBlurRadius", 3.0f },
	{ "ssaoDepthThreshold", 0.01f },
	{ "motionBlurEnable", 0 }, { "motionBlurStrength", 0.4f }, { "motionBlurRadial", 0.2f },
	{ "motionBlurSpeedFactor", 0.3f }, { "motionBlurCameraAware", 1 },
	{ "godRaysEnable", 0 }, { "godRaysExposure", 0.0034f }, { "godRaysDecay", 1.0f },
	{ "godRaysDensity", 0.84f }, { "godRaysWeight", 1.0f }, { "godRaysNumSamples", 20 },
	{ "heightFogEnable", 0 }, { "heightFogDensity", 0.002f }, { "heightFogHeightFalloff", 0.8f },
	{ "heightFogStartHeight", 0.0f }, { "heightFogR", 0.0f }, { "heightFogG", 0.0f },
	{ "heightFogB", 0.0f }, { "heightFogTimecycleScale", 1.0f },
	{ "sunCoronaIntensity", 0.4f }, { "sunCoreIntensity", 0.6f },
	{ "sunStreakIntensity", 0.5f }, { "sunStreakSize", 0.6f },
	{ "sunGlare", 0 }, { "coronaZtest", -1 },
	{ "tonemapAutoExposure", 1 }, { "tonemapAdaptSpeed", 0.12f }, { "tonemapKeyStrength", 1.0f },
	{ "tonemapMinExposure", 0.5f }, { "tonemapMaxExposure", 1.0f },
	{ "tonemapCurveLows", 0.0f }, { "tonemapCurveHighs", 0.0f }, { "tonemapCurveMid", 0.5f },
	{ "tonemapBlackLift", 0.0f }, { "tonemapOperator", 0 },
	{ "dofEnable", 0 }, { "dofFocusDistance", 20.0f }, { "dofFocusRange", 15.0f },
	{ "dofBokehRadius", 4.0f }, { "dofMaxBlur", 0.5f },
	{ "bloomEnable", 0 }, { "bloomQuality", 2 }, { "bloomThreshold", 1.0f }, { "bloomIntensity", 0.8f },
	{ "sunShaftsEnable", 0 }, { "sunShaftsDensity", 0.84f }, { "sunShaftsDecay", 1.0f },
	{ "fxaaEnable", 0 }, { "grainIntensity", 0.0f }, { "caIntensity", 0.0f },
	{ "vignetteIntensity", 0.0f }, { "vignetteRadius", 0.0f }, { "vignetteContrast", 0.0f },
	{ "ivDesaturation", 0.15f }, { "ivGamma", 1.0f }, { "ivSaturation", 0.3f },
	{ "ivCurves", 1.0f }, { "ivVignetteIntensity", 0.15f }, { "ivVignetteRadius", 0.70f },
	{ "ivVignetteContrast", 1.5f }, { "ivBloomIntensity", 0.05f }, { "ivExposure", 2.5f },
	{ "doRadiosity", 0 }, { "radiosity", 1 }, { "radiosityFilterPasses", 2 },
	{ "radiosityRenderPasses", 1 }, { "radiosityIntensity", 35 }, { "radiosityEnable", 1 },
	{ "grainEnable", 1 }, { "grainFilter", 1 }, { "infraredVision", 0 }, { "nightVision", 0 },
	{ "vcsTrails", 0 }, { "trailsLimit", 80 }, { "trailsIntensity", 38 }, { "trailsResolution", 1 },
	{ "sssEnable", 1 }, { "sssIntensity", 0.0f }, { "sssVegIntensity", 0.0f },
	{ "sssSkinIntensity", 0.0f }, { "sssClothIntensity", 0.0f },
	{ "sssPostProcessEnable", 1 }, { "sssPostProcessStrength", 0.2f },
	{ "sssPostProcessRadius", 3.0f }, { "sssPostProcessThreshold", 0.0f }, { "sssAmbientBoost", 1.0f },
	{ "skinEnhanceEnable", 1 }, { "skinWrapFactor", 0.4f }, { "skinSpecularPower", 24.0f },
	{ "skinSpecularStrength", 0.2f }, { "skinSSSStrength", 0.3f },
	{ "hairEnhanceEnable", 1 }, { "hairAnisotropicPower", 48.0f },
	{ "hairAnisotropicStrength", 0.4f }, { "hairSSSStrength", 0.15f },
	{ "vegetationEnhanceEnable", 1 }, { "vegetationSSSStrength", 0.2f },
	{ "vegetationAmbientBoost", 1.3f },
	{ "envMapSize", 512 }, { "envMapUseLODs", 0 }, { "envMapFarClipMult", 2.0f },
	{ "reflectionMsaaLevel", 0 }, { "mirrorDistortionFix", 0 },
	{ "envPower", 20.0f }, { "envFresnel", 0.4f }, { "envShininessMult", 1.0f },
	{ "envSpecularityMult", 1.0f }, { "vehEnvIntensity", 0.4f }, { "vehChromeEnvThreshold", 0.0f },
	{ "vehAutoChrome", 1 }, { "chromeF0", 0.56f }, { "chromeGloss", 0.90f },
	{ "chromeMetallic", 1.0f }, { "chromeEnvBoost", 1.0f }, { "chromeClearcoat", 0.6f },
	{ "leedsShininessMult", 1.0f }, { "neoShininessMult", 1.0f }, { "neoSpecularityMult", 1.0f },
	{ "vehPBRLayers", 255 }, { "vehPrelightFallback", 0.15f }, { "vehEnvFallback", 1 },
	{ "vehEnvStrengthMode", 1 }, { "vehEnvMask", 1 }, { "vehWetEnv", 1 }, { "vehEnvGlint", 0.0f },
	{ "bldEnvReflect", 1 }, { "bldSkyReflect", 0.15f }, { "pbrIblAmbientWeight", 0.5f },
	{ "pbrAmbientFloor", 0.0f }, { "ambientMult", 1.0f }, { "ambientFloor", 0.03f },
	{ "normalMapEnable", 0 }, { "normalMapIntensity", 1.0f }, { "normalMapPlayerOnly", 1 },
	{ "normalBufferEnable", 1 }, { "normalBufferOffset", 0.5f }, { "normalBufferScale", 1.0f },
	{ "velocityBufferEnable", 1 }, { "depthHookEnable", 1 }, { "ragdollEnable", 1 },
	{ "pipeChainEnable", 0 }, { "pipeChainIntensity", 0.3f }, { "pipeChainReflEnable", 0 },
	{ "texAlphaClassify", 1 }, { "stateBaselineEnable", 1 }, { "defaultCbShaderHygiene", 1 },
	{ "waterParallaxEnable", 1 }, { "waterParallaxScale", 1.0f }, { "waterNormalStrength", 1.0f },
	{ "waterFresnelPower", 5.0f }, { "waterSpecularPower", 512.0f },
	{ "waterSpecularIntensity", 1.0f }, { "waterUnderwaterFog", 1.0f },
	{ "waterFoamThreshold", 0.6f }, { "waterFoamSoftness", 0.2f },
	{ "waterShoreFoam", 1 }, { "waterBodyClassify", 1 }, { "waterFresnelSplit", 1 },
	{ "waterShallowR", 0.13f }, { "waterShallowG", 0.45f }, { "waterShallowB", 0.55f },
	{ "waterDeepR", 0.01f }, { "waterDeepG", 0.03f }, { "waterDeepB", 0.08f },
	{ "waterTileScale", 1.0f }, { "waterShoreFade", 2.0f }, { "waterTranslucency", 1.0f },
	{ "waterReflectionStrength", 0.55f }, { "waterUseTimecycle", 1 },
	{ "treeWind", 0 }, { "treeFx", 0 }, { "grassInstancing", 0 },
	{ "grassAddAmbient", 0 }, { "grassBackfaceCull", 1 }, { "grassFixPlacement", 0 },
	{ "detailMaps", 1 }, { "noiseDetail", 1 }, { "usePCTimecyc", 0 },
	{ "altTimecycEnable", 0 }, { "lightningIlluminatesWorld", 0 },
	{ "dualPass", 0 }, { "dualPassDefault", 0 }, { "dualPassBuilding", 0 },
	{ "dualPassVehicle", 0 }, { "dualPassPed", 0 }, { "dualPassGrass", 0 },
	{ "ps2Modulate", 0 }, { "ps2ModulateBuilding", 0 }, { "ps2ModulateGrass", 0 },
	{ "zwriteThreshold", 128 }, { "zwriteThresholdGrass", 128 }, { "zwriteThresholdPed", 128 },
	{ "pedShadows", -1 }, { "stencilShadows", -1 },
	{ "neoWaterDrops", 1 }, { "neoBloodDrops", 0 },
	{ "cloudRenderFix", 1 }, { "forwardPlusEnable", 1 }, { "forwardPlusDebug", 0 },
	{ "profilerEnable", 1 }, { "profilerGpuTimers", 0 }, { "postfxDumpDebug", 0 },
	{ "NoiseQuality", 0 },
	{ "YCbCrCorrection", 0 }, { "lumaScale", 219.0f/255.0f }, { "lumaOffset", 16.0f/255.0f },
	{ "CbScale", 1.23f }, { "CbOffset", 0.0f }, { "CrScale", 1.23f }, { "CrOffset", 0.0f },
	{ "rgb1Mult", 1.0f }, { "rgb2Mult", 1.0f },
	{ "blurLeft", 0 }, { "blurTop", 0 }, { "blurRight", 0 }, { "blurBottom", 0 },
	// m0970 bisect lane — reset targets mirror readIniFile's parse defaults
	// (1 = the compiled always-on behaviour; shadowContractEnable is 0,
	// pedCullMode is the -1 "leave the game alone" sentinel).
	{ "spriteStateShield", 1 }, { "shadowContractEnable", 0 },
	{ "coronaBlendShield", 1 }, { "spriteFlushShield", 1 }, { "effectsAlphaBlock", 1 },
	{ "camRasterRepair", 1 }, { "moonMaskAlpha", 1 }, { "skidmarkAlphaFix", 1 },
	{ "pedTexResync", 1 }, { "pedOpaqueContract", 1 }, { "pedAlphaTestEnable", 1 },
	{ "pedCullMode", -1 }, { "pedAmbientFloor", 0.20f },
	// Coverage-gap / diagnostics rows added with the master-detail restructure.
	{ "waterReflectionMix", 0.7f }, { "diagLevel", 0 }, { "debugOverlay", 0 },
};

static bool
RowDefaultValue(const SettingRow &r, float *out)
{
	if(!out || !r.key)
		return false;
	for(size_t i = 0; i < sizeof(kKeyDefaults)/sizeof(kKeyDefaults[0]); i++){
		if(strcmp(kKeyDefaults[i].key, r.key) == 0){
			*out = kKeyDefaults[i].value;
			return true;
		}
	}
	return false;
}

// Safe enum-name lookup for a NULL-terminated names table. The index comes
// from Config fields that readint-parsed keys (treeFx, waterQuality,
// tonemapOperator, bloomQuality, lightningIlluminatesWorld) can hold
// unclamped, so i can point past the table — indexing r.names[i] directly
// would read past the static array and could hand a garbage pointer to %s.
// Scans to the terminator instead; returns NULL when i is out of range.
static const char *
EnumNameAt(const char *const *names, int i)
{
	if(!names || i < 0)
		return NULL;
	for(int k = 0; k <= i; k++)
		if(!names[k])
			return NULL;	// i is past the table
	return names[i];
}

static void
RowDefaultStr(const SettingRow &r, float d, char *buf, int n)
{
	if(r.kind == RK_BOOL || r.kind == RK_BOOL8)
		snprintf(buf, n, "%d (%s)", d != 0.0f ? 1 : 0, d != 0.0f ? "on" : "off");
	else if(r.kind == RK_ENUM && r.names){
		int i = (int)d - r.base;
		const char *nm = EnumNameAt(r.names, i);
		if(nm)
			snprintf(buf, n, "%s (%d)", nm, (int)d);
		else
			snprintf(buf, n, "%d", (int)d);
	}else if(r.kind == RK_INT)
		snprintf(buf, n, "%d", (int)d);
	else
		snprintf(buf, n, "%g", (double)d);
}

// Hard runtime override: the value is rewritten every frame by a writer the
// menu cannot win against. Only the PBR -> Modern colour-filter lock is in
// this class today (readIniFile + derivePipelineFromPipes + the postfx gate
// all force it). Returns the effective value string and the reason.
static bool
RowHardOverride(const SettingRow &r, char *eff, int effSize, const char **why)
{
	if(eff && effSize > 0)
		eff[0] = '\0';
	if(why)
		*why = NULL;
	if(!config)
		return false;
	if(strcmp(r.key, "colorFilter") == 0 &&
	   (config->pipeline == PIPELINE_PBR || config->buildingPipe == BUILDING_PBR)){
		if(eff && effSize > 0)
			snprintf(eff, effSize, "%s (%d)", s_cfNames[COLORFILTER_MODERN], COLORFILTER_MODERN);
		if(why)
			*why = "PBR forces the colour filter to Modern every frame "
			       "(readIniFile, derivePipelineFromPipes and ColourFilter_switch).";
		return true;
	}
	return false;
}

// "Stored but not currently in effect" marker for detail controls whose
// master switch / mode gate is off. These stay editable (so a developer can
// stage a value before enabling the effect) but are dimmed and the tooltip
// says why the live value is not doing anything yet.
static const char *
RowInactiveReason(const SettingRow &r)
{
	if(!config)
		return NULL;
	const char *k = r.key;
	if(strcmp(k, "colorFilter") == 0 && !config->colorFilterEnable)
		return "The colour-filter pass is bypassed (colorFilterEnable=0); the selected filter is not applied.";
	if((strcmp(k, "waterShallowR") == 0 || strcmp(k, "waterShallowG") == 0 ||
	    strcmp(k, "waterShallowB") == 0 || strcmp(k, "waterDeepR") == 0 ||
	    strcmp(k, "waterDeepG") == 0 || strcmp(k, "waterDeepB") == 0) &&
	   config->waterUseTimecycle)
		return "waterUseTimecycle=1: water colour comes from the timecycle, so this override is inactive.";
	if((strcmp(k, "pipeChainIntensity") == 0 || strcmp(k, "pipeChainReflEnable") == 0) &&
	   !config->pipeChainEnable)
		return "Pipe chain is off (pipeChainEnable=0).";
	if((strcmp(k, "normalMapEnable") == 0 || strcmp(k, "normalMapIntensity") == 0 ||
	    strcmp(k, "normalMapPlayerOnly") == 0) && !gHasExternalNormalMapPlugin)
		return "No RW normal-map plugin detected (gHasExternalNormalMapPlugin=0).";
	if((strcmp(k, "normalMapIntensity") == 0 || strcmp(k, "normalMapPlayerOnly") == 0) &&
	   !config->normalMapEnable)
		return "Normal mapping is off (normalMapEnable=0).";
	if(strcmp(k, "sunGlare") == 0 && !iCanHasSunGlare)
		return "This game build has no sun-glare hook (iCanHasSunGlare=0).";
	if(strcmp(k, "neoWaterDrops") == 0 && !iCanHasNeoDrops)
		return "Neo water drops are unavailable in this game build (iCanHasNeoDrops=0).";
	if(strcmp(k, "forwardPlusDebug") == 0 && !config->forwardPlusEnable)
		return "Forward+ is off (forwardPlusEnable=0); the debug overlay is a no-op.";
	if((strcmp(k, "tonemapAdaptSpeed") == 0 || strcmp(k, "tonemapKeyStrength") == 0 ||
	    strcmp(k, "tonemapMinExposure") == 0 || strcmp(k, "tonemapMaxExposure") == 0) &&
	   !config->tonemapAutoExposure)
		return "Frame-adaptive exposure is off (tonemapAutoExposure=0): timecyc exposure wins.";
	if((strcmp(k, "ivDesaturation") == 0 || strcmp(k, "ivGamma") == 0 ||
	    strcmp(k, "ivSaturation") == 0 || strcmp(k, "ivCurves") == 0 ||
	    strcmp(k, "ivVignetteIntensity") == 0 || strcmp(k, "ivVignetteRadius") == 0 ||
	    strcmp(k, "ivVignetteContrast") == 0 || strcmp(k, "ivBloomIntensity") == 0 ||
	    strcmp(k, "ivExposure") == 0) && !config->ivMode)
		return "IV grade is off (ivMode=0); the IV pipe path may still run, but this grade does not.";
	if((strcmp(k, "motionBlurStrength") == 0 || strcmp(k, "motionBlurRadial") == 0 ||
	    strcmp(k, "motionBlurSpeedFactor") == 0 || strcmp(k, "motionBlurCameraAware") == 0) &&
	   !config->motionBlurEnable)
		return "Motion blur is off (motionBlurEnable=0).";
	if((strcmp(k, "bloomQuality") == 0 || strcmp(k, "bloomThreshold") == 0 ||
	    strcmp(k, "bloomIntensity") == 0) && !config->bloomEnable)
		return "Bloom is off (bloomEnable=0).";
	if((strcmp(k, "sunShaftsDensity") == 0 || strcmp(k, "sunShaftsDecay") == 0) &&
	   !config->sunShaftsEnable)
		return "Sun shafts are off (sunShaftsEnable=0).";
	if((strcmp(k, "dofFocusDistance") == 0 || strcmp(k, "dofFocusRange") == 0 ||
	    strcmp(k, "dofBokehRadius") == 0 || strcmp(k, "dofMaxBlur") == 0) &&
	   !config->dofEnable)
		return "Depth of field is off (dofEnable=0).";
	if((strcmp(k, "godRaysExposure") == 0 || strcmp(k, "godRaysDecay") == 0 ||
	    strcmp(k, "godRaysDensity") == 0 || strcmp(k, "godRaysWeight") == 0 ||
	    strcmp(k, "godRaysNumSamples") == 0) && !config->godRaysEnable)
		return "God rays are off (godRaysEnable=0).";
	if((strcmp(k, "heightFogDensity") == 0 || strcmp(k, "heightFogHeightFalloff") == 0 ||
	    strcmp(k, "heightFogStartHeight") == 0 || strcmp(k, "heightFogR") == 0 ||
	    strcmp(k, "heightFogG") == 0 || strcmp(k, "heightFogB") == 0 ||
	    strcmp(k, "heightFogTimecycleScale") == 0) && !config->heightFogEnable)
		return "Height fog is off (heightFogEnable=0).";
	if((strcmp(k, "sssPostProcessStrength") == 0 || strcmp(k, "sssPostProcessRadius") == 0 ||
	    strcmp(k, "sssPostProcessThreshold") == 0 || strcmp(k, "sssAmbientBoost") == 0) &&
	   !config->sssPostProcessEnable)
		return "SSS post-process blur is off (sssPostProcessEnable=0).";
	if((strcmp(k, "skinWrapFactor") == 0 || strcmp(k, "skinSpecularPower") == 0 ||
	    strcmp(k, "skinSpecularStrength") == 0 || strcmp(k, "skinSSSStrength") == 0) &&
	   !config->skinEnhanceEnable)
		return "Skin enhancement is off (skinEnhanceEnable=0).";
	if((strcmp(k, "hairAnisotropicPower") == 0 || strcmp(k, "hairAnisotropicStrength") == 0 ||
	    strcmp(k, "hairSSSStrength") == 0) && !config->hairEnhanceEnable)
		return "Hair enhancement is off (hairEnhanceEnable=0).";
	if((strcmp(k, "vegetationSSSStrength") == 0 || strcmp(k, "vegetationAmbientBoost") == 0) &&
	   !config->vegetationEnhanceEnable)
		return "Vegetation enhancement is off (vegetationEnhanceEnable=0).";
	if((strcmp(k, "trailsLimit") == 0 || strcmp(k, "trailsIntensity") == 0 ||
	    strcmp(k, "trailsResolution") == 0) && !config->vcsTrails)
		return "VCS trails are off (vcsTrails=0).";
	if(strcmp(k, "grainIntensity") == 0 && !config->grainEnable)
		return "Film grain is off (grainEnable=0).";
	// texAlphaClassify is read by the PBR building callback and the Env
	// vehicle callback only (buildingPipe.cpp:2111, vehiclePipe.cpp:2246).
	// CAR_ENV, CAR_MODERN and (non-IV) CAR_NEO all dispatch that Env
	// callback (vehiclePipe.cpp:3124-3156), so they keep the key live;
	// any other combination never reaches the classifier.
	if(strcmp(k, "texAlphaClassify") == 0 &&
	   config->buildingPipe != BUILDING_PBR &&
	   config->vehiclePipe != CAR_ENV &&
	   config->vehiclePipe != CAR_MODERN &&
	   config->vehiclePipe != CAR_NEO)
		return "The texture-alpha classifier runs only in the PBR building callback "
		       "and the Env/Modern/Neo vehicle callbacks; the active pipes do not call it.";
	if((strcmp(k, "waterParallaxScale") == 0 || strcmp(k, "waterNormalStrength") == 0 ||
	    strcmp(k, "waterFresnelPower") == 0 || strcmp(k, "waterSpecularPower") == 0 ||
	    strcmp(k, "waterSpecularIntensity") == 0 || strcmp(k, "waterUnderwaterFog") == 0 ||
	    strcmp(k, "waterFoamThreshold") == 0 || strcmp(k, "waterFoamSoftness") == 0 ||
	    strcmp(k, "waterShoreFoam") == 0 || strcmp(k, "waterBodyClassify") == 0 ||
	    strcmp(k, "waterTileScale") == 0 || strcmp(k, "waterShoreFade") == 0 ||
	    strcmp(k, "waterTranslucency") == 0 || strcmp(k, "waterReflectionStrength") == 0 ||
	    strcmp(k, "waterStyle") == 0 || strcmp(k, "waterQuality") == 0 ||
	    strcmp(k, "waterUseTimecycle") == 0) &&
	   !config->waterParallaxEnable)
		return "The custom water pipe is off (waterParallaxEnable=0): the game's water draw runs.";
	return NULL;
}

// A setting that is live but currently disagrees with the pipe family it
// belongs to. Unlike RowInactiveReason this must NOT dim the row: the
// control is still the thing that repairs the state. ivMode is the one
// case today - the IV grade and the IV pipe family can be toggled
// independently (a pipe edit only re-syncs ivMode when it crosses the
// family boundary), so both mismatch directions need to be visible on
// the row itself, not only in the All Settings diagnostics block.
static const char *
RowWarnReason(const SettingRow &r)
{
	if(!config || !r.key)
		return NULL;
	if(strcmp(r.key, "ivMode") == 0){
		if(PipeFamilyIsIV() && !config->ivMode)
			return "IV pipe family is active but the IV grade is off (explicit ivMode=0).";
		if(!PipeFamilyIsIV() && config->ivMode)
			return "IV grade is on over a non-IV pipe family (explicit ivMode=1).";
	}
	return NULL;
}

// Do the reset with the same write path a normal edit takes, so onEdit hooks
// (pipeline remap, IV-mode manager, profiler globals, tree profile) stay in
// sync instead of leaving a half-applied UI.
static bool RowIsGrey(const SettingRow &r);		// defined below
static bool FinishRowEdit(const SettingRow &r, bool changed);	// defined below

// One write path for a numeric row value, shared by "Reset to default" and
// "Reset to mode default". Callers do the grey/pointer checks. RK_NOISEQ is
// not a Config field, so it stays in ResetRowToDefault.
static bool
RowStoreNumeric(const SettingRow &r, float d)
{
	if(!r.ptr)
		return false;
	switch(r.kind){
	case RK_BOOL:      *(int*)r.ptr = d != 0.0f ? 1 : 0; break;
	case RK_BOOL8:     *(bool*)r.ptr = d != 0.0f; break;
	case RK_INT:       *(int*)r.ptr = (int)d; break;
	case RK_FLOAT:     *(float*)r.ptr = d; break;
	case RK_ENUM:      *(int*)r.ptr = (int)d; break;
	default:
		return false;
	}
	return FinishRowEdit(r, true);
}

// True when the row binds a numeric Config field (bool/int/float; enums are
// int-backed). RK_NOISEQ, static and INI-only rows fail, which is what the
// disabled "Reset to mode default" item explains.
static bool
RowModeValueWritable(const SettingRow &r)
{
	if(!r.ptr)
		return false;
	switch(r.kind){
	case RK_BOOL: case RK_BOOL8: case RK_INT: case RK_FLOAT: case RK_ENUM:
		return true;
	default:
		return false;
	}
}

static bool
ResetRowToDefault(const SettingRow &r)
{
	if(RowIsGrey(r) || (!r.ptr && r.kind != RK_NOISEQ))
		return false;
	float d = 0.0f;
	if(!RowDefaultValue(r, &d))
		return false;
	if(r.kind == RK_NOISEQ){
		menuSetNoiseQuality((int)d);
		return true;
	}
	return RowStoreNumeric(r, d);
}

// Right-click context menu on the row label. "Reset to default" is only
// offered when kKeyDefaults carries a parser default for the key.
static void
RowResetPopup(const SettingRow &r)
{
	if(RowIsGrey(r) || (!r.ptr && r.kind != RK_NOISEQ))
		return;
	char id[128];
	snprintf(id, sizeof(id), "##rowreset_%s_%s", r.key, r.grp ? r.grp : "");
	if(!ImGui::BeginPopupContextItem(id))
		return;
	float d = 0.0f;
	if(RowDefaultValue(r, &d)){
		char val[64];
		RowDefaultStr(r, d, val, sizeof(val));
		char item[96];
		snprintf(item, sizeof(item), "Reset to default (%s)", val);
		if(ImGui::MenuItem(item)){
			ResetRowToDefault(r);
			if(dbglog_throttle("row_reset"))
				dbglog("[Menu] reset %s -> %g", r.key, (double)d);
		}
	}else{
		ImGui::MenuItem("No parser default for this key", NULL, false, false);
	}

	// Second item: the active mode's profile value for THIS row's INI key
	// (r.key is the exact skygfx.ini name, the same string the manager
	// matches). Mode-owned values are one-shot defaults - the control stays
	// editable and a user edit simply overrides the seeded value.
	if(ModeManager_GetMode() != MODE_NONE && ModeManager_IsOwnedKey(r.key)){
		float mv = 0.0f;
		const bool haveValue = ModeManager_GetOwnedValue(r.key, &mv);
		if(haveValue && RowModeValueWritable(r)){
			char val[64];
			RowDefaultStr(r, mv, val, sizeof(val));
			char item[112];
			snprintf(item, sizeof(item), "Reset to mode default (%s)", val);
			if(ImGui::MenuItem(item)){
				ModeId mode = ModeManager_GetMode();
				if(RowStoreNumeric(r, mv) && dbglog_throttle("row_mode_reset"))
					dbglog("[Menu] mode reset %s -> %g (%s)", r.key, (double)mv,
						ModeManager_GetAlias(mode));
			}
		}else{
			ImGui::MenuItem("Reset to mode default", NULL, false, false);
			if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("This row does not bind a numeric Config field "
					"(int/float/bool), so the mode value cannot be written here.");
		}
	}
	ImGui::EndPopup();
}

// ============================================================
// Coverage view grouping. Every live key gets exactly one group so the
// "All Settings" tab is a complete, searchable inventory rather than a
// second copy of the per-subsystem tabs.
// ============================================================
static const char *const kCoverageGroupOrder[] = {
	"Pipelines & Presets",
	"Lighting & Ambient",
	"Coronas & Light Sprites",
	"Alpha/Blend/State",
	"Water",
	"PostFX",
	"PipeChain & Reflections",
	"Normal Maps",
	"Forward+",
	"Vehicles & Chrome",
	"Vegetation & Grass",
	"World & Weather",
	"Performance & Logging",
	"Debug Probes",
	"Utility & Hotkeys",
	"Other",
};
enum {
	CG_PIPELINES = 0, CG_LIGHT, CG_CORONA, CG_STATE, CG_WATER, CG_POSTFX,
	CG_PIPECHAIN, CG_NORMAL, CG_FORWARD, CG_VEHICLE, CG_VEG, CG_WORLD,
	CG_PERF, CG_DEBUG, CG_UTIL, CG_OTHER
};

static int
CoverageGroupIndex(const char *k)
{
	if(!k)
		return CG_OTHER;
	// exact keys first (prefix tests would catch several of these wrongly)
	if(!strcmp(k, "pipeline") || !strcmp(k, "pipelineOverride") ||
	   !strcmp(k, "qualityPreset") || !strcmp(k, "gamePreset") ||
	   !strcmp(k, "buildingPipe") || !strcmp(k, "vehiclePipe") ||
	   !strcmp(k, "waterStyle") || !strcmp(k, "waterQuality") ||
	   !strcmp(k, "colorFilter") || !strcmp(k, "ivMode"))
		return CG_PIPELINES;
	if(!strcmp(k, "sunShaftsEnable") || !strcmp(k, "sunShaftsDensity") ||
	   !strcmp(k, "sunShaftsDecay"))
		return CG_POSTFX;
	if(!strncmp(k, "sun", 3) || strstr(k, "corona"))
		return CG_CORONA;
	if(!strcmp(k, "waterFresnelSplit"))
		return CG_WATER;
	if(!strncmp(k, "water", 5))
		return CG_WATER;
	if(!strcmp(k, "forwardPlusFallback"))
		return CG_FORWARD;
	if(!strncmp(k, "forwardPlus", 11))
		return CG_FORWARD;
	if(!strncmp(k, "pipeChain", 9) || !strncmp(k, "vehEnv", 6) ||
	   !strncmp(k, "vehWet", 6) ||
	   !strncmp(k, "envMap", 6) || !strncmp(k, "reflectionMsaa", 14) ||
	   !strcmp(k, "mirrorDistortionFix") || !strncmp(k, "bld", 3) ||
	   !strcmp(k, "envPower") || !strcmp(k, "envFresnel") ||
	   !strcmp(k, "envShininessMult") || !strcmp(k, "envSpecularityMult"))
		return CG_PIPECHAIN;
	if(!strncmp(k, "chrome", 6) || !strcmp(k, "vehAutoChrome") ||
	   !strcmp(k, "vehPBRLayers") || !strcmp(k, "vehPrelightFallback") ||
	   !strcmp(k, "vehChromeEnvThreshold") || !strncmp(k, "veh", 3) ||
	   !strncmp(k, "leeds", 5) || !strncmp(k, "neo", 3))
		return CG_VEHICLE;
	if(!strncmp(k, "normal", 6) || !strncmp(k, "velocity", 8) ||
	   !strcmp(k, "depthHookEnable"))
		return CG_NORMAL;
	if(!strncmp(k, "profiler", 8) || !strcmp(k, "postfxDumpDebug") ||
	   !strcmp(k, "NoiseQuality") || !strcmp(k, "diagLevel"))
		return CG_PERF;
	if(!strcmp(k, "debugOverlay"))
		return CG_DEBUG;
	if(!strncmp(k, "ssao", 4) || !strncmp(k, "smaa", 4) || !strcmp(k, "fxaaEnable") ||
	   !strncmp(k, "motionBlur", 10) || !strncmp(k, "tonemap", 7) ||
	   !strncmp(k, "iv", 2) || !strncmp(k, "bloom", 5) || !strncmp(k, "dof", 3) ||
	   !strcmp(k, "grainIntensity") || !strcmp(k, "grainEnable") ||
	   !strcmp(k, "grainFilter") || !strcmp(k, "caIntensity") ||
	   !strncmp(k, "vignette", 8) || !strncmp(k, "radiosity", 9) ||
	   !strcmp(k, "doRadiosity") || !strncmp(k, "sss", 3) ||
	   !strncmp(k, "skin", 4) || !strncmp(k, "hair", 4) ||
	   !strncmp(k, "godRays", 7) || !strncmp(k, "heightFog", 9) ||
	   // Colour-filter crop / YCbCr grade, VCS trails and the two
	   // vision filters have no prefix in common with the groups
	   // above, so they used to fall through to "Other".
	   !strcmp(k, "vcsTrails") || !strncmp(k, "trails", 6) ||
	   !strcmp(k, "infraredVision") || !strcmp(k, "nightVision") ||
	   !strcmp(k, "YCbCrCorrection") || !strncmp(k, "luma", 4) ||
	   !strncmp(k, "Cb", 2) || !strncmp(k, "Cr", 2) ||
	   !strncmp(k, "rgb1Mult", 8) || !strncmp(k, "rgb2Mult", 8) ||
	   !strncmp(k, "blur", 4))
		return CG_POSTFX;
	if(!strcmp(k, "texAlphaClassify") || !strcmp(k, "stateBaselineEnable") ||
	   !strcmp(k, "defaultCbShaderHygiene") || !strcmp(k, "cloudRenderFix") ||
	   !strcmp(k, "colorFilterEnable") || !strncmp(k, "dualPass", 8) ||
	   !strncmp(k, "ps2Modulate", 11) || !strncmp(k, "zwriteThreshold", 15) ||
	   !strcmp(k, "backfaceCull") || !strcmp(k, "detailMaps") ||
	   !strcmp(k, "noiseDetail"))
		return CG_STATE;
	if(!strncmp(k, "grass", 5) || !strncmp(k, "tree", 4) ||
	   !strncmp(k, "vegetation", 10))
		return CG_VEG;
	if(!strncmp(k, "ambient", 7) || !strcmp(k, "pbrIblAmbientWeight") ||
	   !strncmp(k, "pbrAmbient", 10) || !strcmp(k, "lightningIlluminatesWorld") ||
	   !strcmp(k, "pedShadows") || !strcmp(k, "stencilShadows"))
		return CG_LIGHT;
	if(!strncmp(k, "keySwitch", 9) || !strncmp(k, "keyReload", 9) ||
	   !strcmp(k, "ps2grassFiles"))
		return CG_UTIL;
	if(!strncmp(k, "disable", 7) || !strcmp(k, "privateHooks") ||
	   !strcmp(k, "forceWindShader") || !strcmp(k, "transparentLockon") ||
	   !strcmp(k, "fixShadows") || !strcmp(k, "fixPcCarLight") ||
	   !strcmp(k, "explicitBuildingPipe"))
		return CG_DEBUG;
	if(!strcmp(k, "usePCTimecyc") || !strcmp(k, "altTimecycEnable") ||
	   !strcmp(k, "lightningIlluminatesWorld") || !strcmp(k, "ragdollEnable"))
		return CG_WORLD;
	return CG_OTHER;
}

static std::vector<SettingRow> &
GetCoverageRows(void)
{
	static std::vector<SettingRow> rows;
	static Config *rowsCfg = NULL;
	static size_t srcCount = 0;
	if(!config)
		return rows;
	std::vector<SettingRow> &src = GetSettingRows();
	if(rows.empty() || rowsCfg != config || srcCount != src.size()){
		rows.clear();
		std::vector<std::string> seen;
		for(size_t i = 0; i < src.size(); i++){
			const SettingRow &r = src[i];
			bool dup = false;
			for(size_t j = 0; j < seen.size(); j++)
				if(seen[j] == r.key){ dup = true; break; }
			if(dup)
				continue;
			seen.push_back(r.key);
			SettingRow c = r;
			c.tab = "All Settings";
			c.grp = kCoverageGroupOrder[CoverageGroupIndex(r.key)];
			rows.push_back(c);
		}
		std::stable_sort(rows.begin(), rows.end(), [](const SettingRow &a, const SettingRow &b){
			return CoverageGroupIndex(a.key) < CoverageGroupIndex(b.key);
		});
		rowsCfg = config;
		srcCount = src.size();
	}
	return rows;
}

static bool
RowIsGrey(const SettingRow &r)
{
	return r.kind == RK_STATIC || r.kind == RK_STATICB || r.kind == RK_NONE ||
	       r.kind == RK_INIINT;
}

static const char *
RowSection(const SettingRow &r)
{
	return r.kind == RK_NOISEQ ? "General" : "SkyGfx";
}

static void
RowValueStr(const SettingRow &r, char *buf, int n)
{
	switch(r.kind){
	case RK_BOOL:
		snprintf(buf, n, "%d", (*(int*)r.ptr) ? 1 : 0);
		break;
	case RK_BOOL8:
		snprintf(buf, n, "%d", (*(bool*)r.ptr) ? 1 : 0);
		break;
	case RK_INT:
		snprintf(buf, n, "%d", *(int*)r.ptr);
		break;
	case RK_FLOAT:
		snprintf(buf, n, "%g", (double)*(float*)r.ptr);
		break;
	case RK_ENUM:{
		int v = *(int*)r.ptr;
		int i = v - r.base;
		const char *nm = EnumNameAt(r.names, i);
		if(nm)
			snprintf(buf, n, "%s (%d)", nm, v);
		else
			snprintf(buf, n, "%d (out of list)", v);
		break;}
	case RK_STATIC:
		if(r.ptr && strcmp(r.key, "gamePreset") == 0){
			int p = *(int*)r.ptr;
			if(p >= 0 && p < NUM_PRESETS && presetNames[p])
				snprintf(buf, n, "%s (%d)", presetNames[p], p);
			else
				snprintf(buf, n, "Custom (%d)", p);
		}else if(r.ptr)
			snprintf(buf, n, "%d", *(int*)r.ptr);
		else
			snprintf(buf, n, "n/a");
		break;
	case RK_STATICB:
		if(r.ptr)
			snprintf(buf, n, "%d", (*(bool*)r.ptr) ? 1 : 0);
		else
			snprintf(buf, n, "n/a");
		break;
	case RK_INIINT:
		snprintf(buf, n, "%d (from active INI)", ReadCurrentIniInt(r.key, 1));
		break;
	case RK_NOISEQ:{
		int q = menuGetNoiseQuality();
		if(q >= 0 && q <= 3 && s_noise[q])
			snprintf(buf, n, "%s (q=%d)", s_noise[q], q);
		else
			snprintf(buf, n, "%d (>=3 -> 1024)", q);
		break;}
	default:
		snprintf(buf, n, "n/a");
		break;
	}
}

// The control half of a row (checkbox / slider / combo / greyed value).
// Shared by the wide inventory row and the compact main-pane row so both
// write the exact same variable through the exact same code path.
static bool
DrawRowControl(const SettingRow &r, float w)
{
	bool changed = false;
	char wid[96];

	snprintf(wid, sizeof(wid), "##ini_%s_%s", r.key, r.grp ? r.grp : "");

	switch(r.kind){
	case RK_BOOL:{
		// ImGui::Checkbox takes bool* — stage through a bool, store back the
		// int-sized RwBool (little-endian 0/1, same as readIniFile writes).
		bool t = (*(int*)r.ptr) != 0;
		if(ImGui::Checkbox(wid, &t)){
			*(int*)r.ptr = t ? 1 : 0;
			changed = true;
		}
		break;}
	case RK_BOOL8:{
		bool t = *(bool*)r.ptr;
		if(ImGui::Checkbox(wid, &t)){
			*(bool*)r.ptr = t;
			changed = true;
		}
		break;}
	case RK_INT:{
		int t = *(int*)r.ptr;
		ImGui::SetNextItemWidth(w);
		if(ImGui::SliderInt(wid, &t, (int)r.lo, (int)r.hi)){
			*(int*)r.ptr = t;
			changed = true;
		}
		break;}
	case RK_FLOAT:{
		float t = *(float*)r.ptr;
		ImGui::SetNextItemWidth(w);
		if(ImGui::SliderFloat(wid, &t, r.lo, r.hi, "%.3f")){
			*(float*)r.ptr = t;
			changed = true;
		}
		break;}
	case RK_ENUM:{
		int v = *(int*)r.ptr;
		int i = v - r.base;
		char preview[64];
		const char *nm = EnumNameAt(r.names, i);
		if(nm)
			snprintf(preview, sizeof(preview), "%s", nm);
		else
			snprintf(preview, sizeof(preview), "%d (out of list)", v);
		ImGui::SetNextItemWidth(w);
		if(ImGui::BeginCombo(wid, preview)){
			for(int k = 0; r.names && r.names[k]; k++)
				if(ImGui::Selectable(r.names[k], k == i)){
					if(r.base + k != v){
						*(int*)r.ptr = r.base + k;
						changed = true;
					}
				}
			ImGui::EndCombo();
		}
		break;}
	case RK_NOISEQ:{
		int q = menuGetNoiseQuality();
		int shown = (q >= 0 && q <= 3) ? q : 0;
		ImGui::SetNextItemWidth(w);
		if(ImGui::BeginCombo(wid, q >= 0 && q <= 3 ? s_noise[shown] : ">=3 -> 1024 px")){
			for(int k = 0; k < 4; k++)
				if(ImGui::Selectable(s_noise[k], k == shown)){
					menuSetNoiseQuality(k);
					changed = true;
				}
			ImGui::EndCombo();
		}
		break;}
	case RK_STATIC:{
		char b[32];
		RowValueStr(r, b, sizeof(b));
		ImGui::TextDisabled("%s", b);
		break;}
	case RK_STATICB:{
		char b[32];
		RowValueStr(r, b, sizeof(b));
		ImGui::TextDisabled("%s", b);
		break;}
	case RK_INIINT:{
		char b[48];
		RowValueStr(r, b, sizeof(b));
		ImGui::TextDisabled("%s", b);
		break;}
	case RK_NONE:
		ImGui::TextDisabled("(no live variable)");
		break;
	}
	return changed;
}

// Apply the row's onEdit hook + push config into the CPostEffects copies.
static bool
FinishRowEdit(const SettingRow &r, bool changed)
{
	if(changed){
		if(r.onEdit)
			r.onEdit();
		resetValues();	// config -> CPostEffects copy, same as refreshIni()
	}
	return changed;
}

// Tooltip shared by both row shapes: INI key, live value, effective/inactive
// state, and the note. The raw key is always visible here even when the label
// is the human-readable one.
static void
RowKeyTooltip(const SettingRow &r, bool grey, const char *effective,
              const char *overriddenWhy, const char *inactiveWhy,
              const char *warnWhy)
{
	if(!ImGui::IsItemHovered())
		return;
	char val[64];
	RowValueStr(r, val, sizeof(val));
	char tip[1200];
	snprintf(tip, sizeof(tip), "INI key: [%s] %s\nLive value: %s\n",
		RowSection(r), r.key, val);
	char add[512];
	// Slider bounds for numeric rows: the same lo..hi the control clamps
	// to, so the tooltip documents the legal range without opening the INI.
	if(r.kind == RK_INT)
		snprintf(add, sizeof(add), "Range: %d .. %d\n", (int)r.lo, (int)r.hi);
	else if(r.kind == RK_FLOAT)
		snprintf(add, sizeof(add), "Range: %g .. %g\n", (double)r.lo, (double)r.hi);
	else
		add[0] = '\0';
	if(add[0])
		strncat(tip, add, sizeof(tip) - strlen(tip) - 1);
	if(effective && effective[0]){
		snprintf(add, sizeof(add), "In effect: %s\n", effective);
		strncat(tip, add, sizeof(tip) - strlen(tip) - 1);
	}
	// A row can be both overridden and currently bypassed (colorFilter
	// under PBR while the colour-filter pass is off), so report every
	// reason instead of stopping at the first one.
	if(overriddenWhy){
		snprintf(add, sizeof(add), "Overridden at runtime: %s\n", overriddenWhy);
		strncat(tip, add, sizeof(tip) - strlen(tip) - 1);
	}
	if(inactiveWhy){
		snprintf(add, sizeof(add), "Inactive: %s\n", inactiveWhy);
		strncat(tip, add, sizeof(tip) - strlen(tip) - 1);
	}
	if(warnWhy){
		snprintf(add, sizeof(add), "Warning: %s\n", warnWhy);
		strncat(tip, add, sizeof(tip) - strlen(tip) - 1);
	}
	if(r.note){
		snprintf(add, sizeof(add), "%s\n", r.note);
		strncat(tip, add, sizeof(tip) - strlen(tip) - 1);
	}
	float def = 0.0f;
	if(RowDefaultValue(r, &def)){
		char defStr[64];
		RowDefaultStr(r, def, defStr, sizeof(defStr));
		snprintf(add, sizeof(add), "Parser/fresh-install default: %s\n", defStr);
		strncat(tip, add, sizeof(tip) - strlen(tip) - 1);
	}
	if(grey)
		strncat(tip, "No live edit path (read-only/greyed).\n",
			sizeof(tip) - strlen(tip) - 1);
	else if((r.ptr || r.kind == RK_NOISEQ) && !overriddenWhy){
		if(RowDefaultValue(r, &def))
			strncat(tip, "Right-click the control to reset it to this default.",
				sizeof(tip) - strlen(tip) - 1);
	}
	ImGui::SetTooltip("%s", tip);
}

// Startup-only keys: the row has no live edit path because the consumer
// (patch/hook install, load-time seed, boot-time reader) only runs during
// process init. RESTART is narrower than "greyed": a grey row can also be a
// disabled parser or a stored-but-dead knob, and those must not claim that a
// restart would help.
static bool
RowNeedsRestart(const char *key)
{
	static const char *const kRestartKeys[] = {
		"disableClouds", "disableGamma", "fixPcCarLight", "privateHooks",
		"forceWindShader", "transparentLockon", "fixShadows",
		"explicitBuildingPipe", "forwardPlusFallback", "qualityPreset",
		"treeWind", NULL
	};
	for(int i = 0; kRestartKeys[i]; i++)
		if(strcmp(key, kRestartKeys[i]) == 0)
			return true;
	return false;
}

// The five post-processing keys added by the 2026-10 coverage pass. They
// keep a NEW pill until the next inventory refresh; everything else is
// considered established. Keep this list in sync with BuildSettingRows'
// "New post-effects" block rather than flagging the whole feature group.
static bool
RowIsNewKey(const char *key)
{
	static const char *const kNewKeys[] = {
		"dofEnable", "bloomEnable", "sunShaftsEnable", "fxaaEnable", "caIntensity", NULL
	};
	for(int i = 0; kNewKeys[i]; i++)
		if(strcmp(key, kNewKeys[i]) == 0)
			return true;
	return false;
}

// The active mode profile owns this row's INI key (r.key is the exact
// skygfx.ini name the manager matches - never the Config field name).
// MODE_NONE and MODE_CUSTOM own nothing, so no badge can appear for them.
static bool
RowModeOwned(const SettingRow &r)
{
	if(!r.key || ModeManager_GetMode() == MODE_NONE)
		return false;
	return ModeManager_IsOwnedKey(r.key);
}

// "<Alias> sets this when applied; your edit overrides it for this session."
static void
RowModeBadgeTip(const SettingRow &r, char *buf, int n)
{
	if(!buf || n <= 0)
		return;
	buf[0] = '\0';
	if(!RowModeOwned(r))
		return;
	snprintf(buf, n, "%s sets this when applied; your edit overrides it for this session.",
		ModeManager_GetAlias(ModeManager_GetMode()));
}

// One inventory row: [label] [control] [INI key]  — hover the label or key for
// the value + note. Returns true when the user changed something.
static bool
DrawSettingRow(const SettingRow &r)
{
	static const float kCtlX = 260.0f;
	static const float kCtlW = 180.0f;
	static const float kKeyX = 470.0f;
	// Right edge captured before any item is submitted, so the state pill
	// can sit at the end of the row without shifting the key column.
	const float rowRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
	bool grey = RowIsGrey(r);
	char eff[64];
	const char *why = NULL;
	const char *inactive = NULL;
	bool hard = RowHardOverride(r, eff, sizeof(eff), &why);
	// Always evaluate the bypass reason: a hard override and a bypassed
	// pass can both be true, and the tooltip must carry both.
	inactive = RowInactiveReason(r);
	const char *warn = (!hard && !inactive) ? RowWarnReason(r) : NULL;
	bool dim = grey || hard || inactive;

	// column 1 — label
	ImGui::AlignTextToFramePadding();
	if(dim)
		ImGui::TextDisabled("%s", r.label);
	else
		ImGui::TextUnformatted(r.label);
	RowKeyTooltip(r, grey, hard ? eff : NULL, hard ? why : NULL, inactive, warn);

	// column 2 — control (disabled only for a hard runtime override; an
	// inactive detail control stays editable so values can be staged before
	// the master switch is turned on)
	ImGui::SameLine(kCtlX);
	if(hard)
		ImGui::BeginDisabled();
	bool changed = DrawRowControl(r, kCtlW);
	if(hard)
		ImGui::EndDisabled();
	if(!hard)
		RowResetPopup(r);

	// column 3 — INI key + right-aligned state pill. The pill replaces the
	// old "(overridden)/(inactive)" text suffix; the tooltip still carries
	// the full reason. The MODE pill (when the active profile owns this key)
	// sits just left of the state pill and never disables the control.
	ImGui::SameLine(kKeyX);
	ImGui::TextDisabled("%s", r.key);

	DmBadgeKind kind = DM_BADGE_NEW;
	bool haveBadge = true;
	if(hard)
		kind = DM_BADGE_OVERRIDDEN;
	else if(inactive)
		kind = DM_BADGE_INACTIVE;
	else if(warn)
		kind = DM_BADGE_WARN;
	else if(RowNeedsRestart(r.key))
		kind = DM_BADGE_RESTART;
	else if(RowIsNewKey(r.key))
		kind = DM_BADGE_NEW;
	else
		haveBadge = false;

	const float pillRight = rowRight - 4.0f;
	if(RowModeOwned(r)){
		char modeTip[192];
		RowModeBadgeTip(r, modeTip, sizeof(modeTip));
		const float reserved = haveBadge ? StatusBadgeSize(kind).x + 6.0f : 0.0f;
		const float modeRight = pillRight - reserved;
		// Keep the key text readable: skip the MODE pill in very narrow rows
		// instead of letting either pill overlap the key column.
		if(modeRight - ModeOwnedBadgeSize().x >= ImGui::GetItemRectMax().x + 8.0f)
			DrawModeOwnedBadge(modeTip, modeRight);
	}
	if(haveBadge)
		DrawStatusBadge(kind, pillRight);

	return FinishRowEdit(r, changed);
}

// Compact row shape: [label .............. control]. Kept for narrow
// embeddings (DrawRowsFrom's compact flag); the master-detail panes use
// the wide three-column row, but the shape and its write path are identical.
// Label sits on the left, the control is right-aligned so every control in
// the pane forms one clean column; hover the label for the INI key + note.
static bool
DrawSettingRowCompact(const SettingRow &r)
{
	static const float kCtlW = 180.0f;
	bool grey = RowIsGrey(r);
	char eff[64];
	const char *why = NULL;
	const char *inactive = NULL;
	bool hard = RowHardOverride(r, eff, sizeof(eff), &why);
	inactive = RowInactiveReason(r);	// see DrawSettingRow: report both reasons
	const char *warn = (!hard && !inactive) ? RowWarnReason(r) : NULL;
	bool dim = grey || hard || inactive;

	float x = ImGui::GetCursorPosX();			// window-local x at the row start
	float avail = ImGui::GetContentRegionAvail().x;	// to the right content edge
	const float rowStartX = ImGui::GetCursorScreenPos().x;
	float w = kCtlW;
	if(avail - 12.0f < w)
		w = (std::max)(60.0f, avail - 12.0f);	// ImMax needs imgui_internal.h; (std::max) avoids windows.h max macro

	ImGui::AlignTextToFramePadding();
	if(dim)
		ImGui::TextDisabled("%s", r.label);
	else
		ImGui::TextUnformatted(r.label);
	RowKeyTooltip(r, grey, hard ? eff : NULL, hard ? why : NULL, inactive, warn);

	// State pill inline after the label, but only when it fits before the
	// right-aligned control. Compact rows never take the wide row's
	// right-edge pill, so nothing can overlap a slider. A mode-owned row
	// draws the quieter MODE pill first, then the state pill after it.
	const bool restart = RowNeedsRestart(r.key);
	DmBadgeKind kind = DM_BADGE_NEW;
	bool haveBadge = true;
	if(hard)
		kind = DM_BADGE_OVERRIDDEN;
	else if(inactive)
		kind = DM_BADGE_INACTIVE;
	else if(warn)
		kind = DM_BADGE_WARN;
	else if(restart)
		kind = DM_BADGE_RESTART;
	else if(RowIsNewKey(r.key))
		kind = DM_BADGE_NEW;
	else
		haveBadge = false;

	const bool owned = RowModeOwned(r);
	const float modeW = owned ? ModeOwnedBadgeSize().x : 0.0f;
	const float badgeW = haveBadge ? StatusBadgeSize(kind).x : 0.0f;
	const float totalW = modeW + badgeW + ((owned && haveBadge) ? 6.0f : 0.0f);
	const float labelEnd = ImGui::GetItemRectMax().x;
	const float ctlLeftX = rowStartX + avail - w;
	if(totalW > 0.0f && labelEnd + 8.0f + totalW < ctlLeftX - 4.0f){
		float px = labelEnd + 8.0f;
		if(owned){
			char modeTip[192];
			RowModeBadgeTip(r, modeTip, sizeof(modeTip));
			DrawModeOwnedBadge(modeTip, px + modeW);
			px += modeW + 6.0f;
		}
		if(haveBadge)
			DrawStatusBadge(kind, px + badgeW);
	}

	ImGui::SameLine(x + avail - w);
	if(hard)
		ImGui::BeginDisabled();
	bool changed = DrawRowControl(r, w);
	if(hard)
		ImGui::EndDisabled();
	if(!hard)
		RowResetPopup(r);
	return FinishRowEdit(r, changed);
}

// Case-insensitive substring (ASCII; keys/labels are ASCII).
static bool
ContainsCI(const char *hay, const char *needle)
{
	if(!hay || !needle || !needle[0])
		return true;
	for(const char *h = hay; *h; h++){
		const char *a = h, *b = needle;
		while(*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)){
			a++;
			b++;
		}
		if(!*b)
			return true;
	}
	return false;
}

static bool
RowMatches(const SettingRow &r, const char *filter)
{
	if(filter == NULL || filter[0] == '\0')
		return true;
	if(ContainsCI(r.key, filter) || ContainsCI(r.label, filter) ||
	   ContainsCI(r.grp, filter) || ContainsCI(r.note, filter))
		return true;
	if(r.names){
		for(int i = 0; r.names[i]; i++)
			if(ContainsCI(r.names[i], filter))
				return true;
	}
	return false;
}

// Draw every row of one tab or of an explicit row list (the All Settings
// coverage view reuses the exact same row/control path). compact=true
// switches to the main-pane row shape: [label ... control], no INI-key column.
static bool
DrawRowsFrom(const std::vector<SettingRow> &rows, const char *tab,
             const char *filter, bool compact = false)
{
	bool changed = false;

	for(size_t i = 0; i < rows.size(); ){
		const SettingRow &first = rows[i];
		if(strcmp(first.tab, tab) != 0){
			i++;
			continue;
		}
		std::string t0 = first.tab, g0 = first.grp;
		size_t j = i;
		bool anyVisible = false;
		for(; j < rows.size() && t0 == rows[j].tab && g0 == rows[j].grp; j++)
			if(RowMatches(rows[j], filter))
				anyVisible = true;

		if(anyVisible){
			// Aero glass group band with its own chevron. State is kept in
			// this window's ImGuiStorage, so it survives tab switches and
			// behaves exactly like a default-open CollapsingHeader.
			bool open = DrawGlassCollapsingHeader(g0.c_str(), true);
			if(open)
				for(size_t k = i; k < j; k++){
					if(!RowMatches(rows[k], filter))
						continue;
					bool drew = compact ? DrawSettingRowCompact(rows[k]) : DrawSettingRow(rows[k]);
					if(drew)
						changed = true;
				}
		}
		i = j;
	}
	return changed;
}

static bool
DrawRowsForTab(const char *tab, const char *filter, bool compact = false)
{
	return DrawRowsFrom(GetSettingRows(), tab, filter, compact);
}

// Modes section — radio rows bound to the graphics mode manager
// (modeManager.cpp). Exactly one mode is active at a time (or none):
// clicking a row runs ModeManager_SetMode(id); clicking the ACTIVE row
// toggles the mode OFF (SetMode's same-id toggle restores the pre-mode
// Config snapshot). The lit radio reflects ModeManager_GetMode() live.
static void
DrawModesSection(void)
{
	static const struct { ModeId id; const char *label; const char *hint; } kModes[] = {
		{ MODE_NONE,   "None",   "No mode active - the live config is yours.\nClick the active mode's row to switch it off (restores the pre-mode config)." },
		{ MODE_PS2,    "PS2",    "PS2 pipe mapping (expand later).\nPipes: building=PS2 vehicle=PS2." },
		{ MODE_XBOX,   "Xbox",   "Xbox pipe mapping (expand later).\nPipes: building=Xbox vehicle=Xbox." },
		{ MODE_MOBILE, "Mobile", "Mobile pipe mapping (expand later).\nPipes: building=Xbox vehicle=Mobile." },
		{ MODE_GTAIV,  "GTAIV",  "GTA IV profile (mirrors IVMode_ApplyDefaults):\nIV pipes + filter, radiosity/SSAO/SMAA/grain off, ivMode on.\nINI-tuned IV grade sliders are kept." },
		{ MODE_GTAV,   "GTAV",   "GTA V placeholder (expand later) - no V pipe mapping exists yet." },
	};

	DrawGlassHeader("Modes", ImVec2(0.0f, 24.0f));
	ImGui::Text("Modes (one active at a time):");
	ModeId cur = ModeManager_GetMode();
	for(int i = 0; i < (int)(sizeof(kModes)/sizeof(kModes[0])); i++){
		if(ImGui::RadioButton(kModes[i].label, cur == kModes[i].id)){
			ModeKeepMenuOpen keepMenu;	// UI flag must survive the Config swap
			ModeManager_SetMode(kModes[i].id);
			PfNote("mode -> %s", kModes[i].label);	// profiler timeline marker
			resetValues();	// config -> CPostEffects copies, same as every other row
		}
		if(ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", kModes[i].hint);
	}
	DrawAccentSeparator();
}

// Preset buttons — the five pipeline presets the plan asks for. PS2/Xbox/PC
// and IV go through ApplyPreset (the same path the Style combo uses);
// "Modern" has no GamePreset entry yet, so it sets the pipes directly
// (derivePipelineFromPipes() then locks pipeline=PBR + Modern colour filter).
static void
DrawPresetsTab(void)
{
	// Modes first: ModeManager owns whole-config snapshots, so a mode
	// switch is best made before touching the preset rows below.
	DrawModesSection();

	static const char *styleStr[] = { "PS2", "Xbox", "PC", "Custom Maxed" };
	int presetIdx = 3;
	if(config->preset == PRESET_SA_PS2) presetIdx = 0;
	else if(config->preset == PRESET_SA_XBOX) presetIdx = 1;
	else if(config->preset == PRESET_SA_PC) presetIdx = 2;
	// This combo is the SINGLE home of the Style setting (the old Graphics
	// tab's copy was removed in the dedup pass). Gate on the Combo result:
	// applying the preset / loading the newest custom config EVERY FRAME
	// while the tab is visible continuously stomped the live config and
	// fought every other config writer (pipes, filter, toggles churned/frame).
	if(ImGui::Combo("Style (gamePreset)", &presetIdx, styleStr, 4)){
		if(presetIdx <= 2){
			static const int kStylePreset[3] = { PRESET_SA_PS2, PRESET_SA_XBOX, PRESET_SA_PC };
			ApplyPreset(config, kStylePreset[presetIdx]);
			resetValues();
		}else{
			// "Custom Maxed" = the newest configs\*.ini
			// (no-op when none exist - keeps current state)
			LoadNewestCustomConfig();
			config->preset = PRESET_CUSTOM;
		}
	}
	if(ImGui::IsItemHovered())
		ImGui::SetTooltip("Applies the preset (or the newest saved config) and stores it as gamePreset.");

	// Full preset list (III/VC/SA/LCS/VCS/IV/Best/max). The four-entry Style
	// combo above is the quick path; this one makes every ApplyPreset entry
	// reachable without editing gamePreset by hand.
	static int s_fullPreset = 0;
	if(config->preset >= 0 && config->preset < NUM_PRESETS)
		s_fullPreset = config->preset;
	if(s_fullPreset < 0 || s_fullPreset >= NUM_PRESETS)
		s_fullPreset = 0;
	ImGui::PushItemWidth(240.0f);
	if(ImGui::Combo("Apply game preset", &s_fullPreset, presetNames, NUM_PRESETS)){
		ApplyPreset(config, s_fullPreset);
		resetValues();
	}
	ImGui::PopItemWidth();
	if(ImGui::IsItemHovered())
		ImGui::SetTooltip("Every preset in presets.cpp. Applying one overwrites the pipe/filter/effect keys\n"
			"the preset carries; the current values below are the live result.");

	DrawGlassHeader("Pipeline presets", ImVec2(0.0f, 24.0f));
	ImGui::Text("Pipeline presets (set pipes + colour filter):");
	if(ImGui::Button("PS2")){ PfNote("preset PS2"); ApplyPreset(config, PRESET_SA_PS2); resetValues(); }
	ImGui::SameLine();
	if(ImGui::Button("Xbox")){ PfNote("preset Xbox"); ApplyPreset(config, PRESET_SA_XBOX); resetValues(); }
	ImGui::SameLine();
	if(ImGui::Button("PC")){ PfNote("preset PC"); ApplyPreset(config, PRESET_SA_PC); resetValues(); }
	ImGui::SameLine();
	if(ImGui::Button("IV")){ PfNote("preset IV"); ApplyPreset(config, PRESET_IV_PC); resetValues(); }
	ImGui::SameLine();
	if(ImGui::Button("Modern")){
		bool wasIV = PipeFamilyIsIV();	// before the direct pipe write below
		config->buildingPipe = BUILDING_PBR;
		config->vehiclePipe = CAR_MODERN;
		config->colorFilter = COLORFILTER_MODERN;
		config->preset = PRESET_CUSTOM;
		// Same defect class as the combo/raw rows: this button sets pipes
		// directly (no ApplyPreset, so nothing else writes ivMode) and must
		// not leave the IV grade compositing over the PBR pipes it just set.
		SyncIvModeFromPipeFamily(wasIV);
		resetValues();
	}
	if(ImGui::IsItemHovered())
		ImGui::SetTooltip("No GamePreset entry for PBR yet - sets the pipes directly.\nderivePipelineFromPipes() then reports pipeline=PBR.");
	ImGui::SameLine();
	if(ImGui::Button("Reload INI"))
		reloadAllInis();

	DrawAccentSeparator();
	ImGui::TextDisabled("PS2    -> building=PS2    vehicle=PS2     filter=PS2");
	ImGui::TextDisabled("Xbox   -> building=Xbox   vehicle=Xbox    filter=PC");
	ImGui::TextDisabled("PC     -> building=Xbox   vehicle=PC      filter=PC");
	ImGui::TextDisabled("IV     -> building=GTAIV  vehicle=GTAIV   filter=GTAIV");
	ImGui::TextDisabled("Modern -> building=PBR    vehicle=Modern  filter=Modern");
	DrawAccentSeparator();
	ImGui::Text("Current: building=%s  vehicle=%s  water=%s  filter=%s",
		s_buildNames[config->buildingPipe >= 0 && config->buildingPipe < 4 ? config->buildingPipe : 3],
		(config->vehiclePipe >= 0 && config->vehiclePipe < 11 && s_vehNames[config->vehiclePipe]) ? s_vehNames[config->vehiclePipe] : "?",
		(config->waterStyle >= 0 && config->waterStyle < 3) ? s_waterSty[config->waterStyle] : "?",
		(config->colorFilter >= 0 && config->colorFilter < 9) ? s_cfNames[config->colorFilter] : "?");
	DrawEffectiveSummary();
}

// INI Keys tab — every row, every key, greys included. Uses the global
// settings filter at the top of the window; a greyed row has no live variable.
static void
DrawReferenceTab(void)
{
	std::vector<SettingRow> &rows = GetSettingRows();

	ImGui::TextDisabled("%d rows. Greyed rows have no live variable (startup-only / load-time-only).",
		(int)rows.size());
	DrawAccentSeparator();

	const char *filter = s_settingFilter;
	std::vector<std::string> seen;	// a key lives on >1 tab on purpose — show it once here
	std::string curTab, curGrp;
	bool headerOpen = false;
	bool haveHeader = false;

	for(size_t i = 0; i < rows.size(); i++){
		const SettingRow &r = rows[i];
		if(!RowMatches(r, filter))
			continue;
		bool dup = false;
		for(size_t k = 0; k < seen.size(); k++)
			if(seen[k] == r.key){ dup = true; break; }
		if(dup)
			continue;
		seen.push_back(r.key);

		std::string t0 = r.tab, g0 = r.grp;
		if(!haveHeader || t0 != curTab || g0 != curGrp){
			curTab = t0; curGrp = g0;
			haveHeader = true;
			std::string title = curTab + " / " + curGrp;
			headerOpen = DrawGlassCollapsingHeader(title.c_str(), true);
		}
		if(headerOpen)
			DrawSettingRow(r);
	}
	if(haveHeader)
		ImGui::TextDisabled("%d unique keys listed", (int)seen.size());
}

// Master switch behind a section: -1 = the section has no on/off switch
// (Tonemap, Water - always usable, never dimmed).
enum EffectSwitch {
	EF_SSAO = 0, EF_MOTION_BLUR, EF_GOD_RAYS, EF_HEIGHT_FOG, EF_SMAA,
	EF_PIPE_CHAIN, EF_RADIOSITY, EF_COLOUR_FILTER, EF_IV_GRADE, EF_SSS,
	EF_VCS_TRAILS, EF_DOF, EF_BLOOM, EF_SUN_SHAFTS, EF_GRAIN
};

// ============================================================
// SECTION REGISTRY — the master-detail navigation model.
//
// The old layout was a two-level tab hierarchy: nine top-level tabs, and
// inside "Main" a fourteen-tab strip with FittingPolicyScroll that
// side-scrolled whenever the window was narrower than the strip. Both
// levels are folded into ONE master-detail pair:
//
//   LEFT  : the section list below, grouped by category, every effect
//           with a master switch carrying a colour-coded ON/OFF pill.
//           Arrow keys move nav focus, Enter opens (ImGui nav + Selectable).
//   RIGHT : the active section's pane - either its SettingRow rows
//           (DrawRowsForTab) or a custom body (drawFn).
//
// The top tab strip is gone: every former top-level tab is a section here,
// so nothing is reachable only through a scrolling strip and each section
// is exactly one click (or two arrow presses) from any other.
// ============================================================
enum MenuCategory {
	MC_SETUP = 0,		// Switchboard, Presets
	MC_EFFECTS,			// the opt-in post/scene effects
	MC_WORLD,			// vehicle / vegetation / peds / shields
	MC_DIAG,			// PostFX checklist, Profiler, Diagnostics
	MC_SYSTEM,			// Utility
	MC_REFERENCE,		// INI Keys, All Settings
	MC__COUNT
};

struct MenuSection {
	const char *id;		// SettingRow::tab id carrying this section's rows
	const char *label;	// nav + pane title
	MenuCategory cat;
	const char *desc;	// one-liner under the pane title / nav tooltip
	int         sw;		// EffectSwitch, or -1 when there is no on/off switch
	void      (*drawFn)(void);	// NULL = generic pane (rows for `id`)
};

// Custom pane bodies (each draws its own glass header):
static void DrawMainPane(void);
static void DrawPresetsPane(void);
static void DrawProfilerPane(void);
static void DrawReferencePane(void);
static void DrawAllSettingsTab(void);	// defined below (own header inside)
static bool EffectSwitchOn(int sw);		// defined below (master-switch readout)

static const MenuSection s_menuSections[] = {
	// --- Setup ------------------------------------------------------------
	{ "Main", "Switchboard", MC_SETUP,
	  "Master switch for every effect, pipes, rendering features and state repairs.",
	  -1, DrawMainPane },
	{ "Presets", "Presets", MC_SETUP,
	  "Modes, style presets and pipe sets.",
	  -1, DrawPresetsPane },
	// --- Effects ------------------------------------------------------------
	{ "SMAA", "SMAA", MC_EFFECTS,
	  "Subpixel morphological anti-aliasing. No other settings - this switch is the whole feature.",
	  EF_SMAA, NULL },
	{ "SSAO", "SSAO", MC_EFFECTS,
	  "Screen-space ambient occlusion (quarter-res temporal overhaul).",
	  EF_SSAO, NULL },
	{ "DOF", "Depth of field", MC_EFFECTS,
	  "Focus distance / range, bokeh radius and max blur.",
	  EF_DOF, NULL },
	{ "Bloom", "Bloom", MC_EFFECTS,
	  "Threshold, quality (mip chain depth) and intensity.",
	  EF_BLOOM, NULL },
	{ "Motion blur", "Motion blur", MC_EFFECTS,
	  "Strength, radial, speed factor and the velocity buffer that feeds it.",
	  EF_MOTION_BLUR, NULL },
	{ "God rays", "God rays", MC_EFFECTS,
	  "Screen-space light shafts: exposure, decay, density, weight, samples.",
	  EF_GOD_RAYS, NULL },
	{ "Sun shafts", "Sun shafts", MC_EFFECTS,
	  "Volumetric sun shafts: density and decay.",
	  EF_SUN_SHAFTS, NULL },
	{ "Height fog", "Height fog", MC_EFFECTS,
	  "Exponential height fog: density, falloff, colour, timecycle scale.",
	  EF_HEIGHT_FOG, NULL },
	{ "Tonemap", "Tonemap", MC_EFFECTS,
	  "Operator LUT, adaptive exposure window and curve shaping. Always active.",
	  -1, NULL },
	{ "Film grain", "Film grain", MC_EFFECTS,
	  "New FX grain pass (intensity) and the legacy grain kernel type.",
	  EF_GRAIN, NULL },
	{ "Colour filter", "Colour filter", MC_EFFECTS,
	  "YCbCr grade, crop offsets and the pass gate. PBR pipes force the Modern filter.",
	  EF_COLOUR_FILTER, NULL },
	{ "IV grade", "IV grade", MC_EFFECTS,
	  "GTA IV colour grade: desaturation, gamma, curves, vignette, bloom, exposure.",
	  EF_IV_GRADE, NULL },
	{ "SSS", "Screen-space SSS", MC_EFFECTS,
	  "Subsurface scattering plus the skin/hair/vegetation material enhancement.",
	  EF_SSS, NULL },
	{ "Radiosity", "Radiosity", MC_EFFECTS,
	  "Type, filter/render passes and intensity.",
	  EF_RADIOSITY, NULL },
	{ "Pipe chain", "Pipe chain / SSR", MC_EFFECTS,
	  "BRDF-classified screen-space reflections packed by the geometry pipes.",
	  EF_PIPE_CHAIN, NULL },
	{ "VCS trails", "VCS trails", MC_EFFECTS,
	  "Trail limit, intensity and resolution.",
	  EF_VCS_TRAILS, NULL },
	{ "Water", "Water", MC_EFFECTS,
	  "Style, quality, appearance and the feature switches of the custom water pipe. Always drawn.",
	  -1, NULL },
	// --- World ------------------------------------------------------------
	{ "Buildings", "Buildings", MC_WORLD,
	  "Building pipe selector (PS2/Xbox/GTAIV/PBR) and the PBR building reflection terms.",
	  -1, NULL },
	{ "Vehicle", "Vehicle", MC_WORLD,
	  "Vehicle pipe selector, chrome BRDF, env map / reflections, PBR vehicle terms, legacy car-pipe shading.",
	  -1, NULL },
	{ "Vegetation", "Vegetation", MC_WORLD,
	  "Grass and tree features: instancing, wind, alpha profile, per-plant variance.",
	  -1, NULL },
	{ "Peds", "Peds", MC_WORLD,
	  "Ped shadows, render-state contracts and the ragdoll gate.",
	  -1, NULL },
	{ "Shields", "Shields", MC_WORLD,
	  "Render-state bisect levers: sprite/corona/flush shields, alpha blocks, raster repairs.",
	  -1, NULL },
	// --- Diagnostics --------------------------------------------------------
	{ "PostFX", "PostFX checklist", MC_DIAG,
	  "Every post pass, one row each, grouped by effect. Switch ONE off at a time to isolate what turns the screen black.",
	  -1, NULL },
	{ "Profiler", "Profiler", MC_DIAG,
	  "Live pass/frame timings, draw counters and shader binds (PF_SCOPE).",
	  -1, DrawProfilerPane },
	{ "Diagnostics", "Diagnostics", MC_DIAG,
	  "Debug readouts and verbosity: PostFX dump, Forward+ debug view, F9 overlay.",
	  -1, NULL },
	// --- System ------------------------------------------------------------
	{ "Utility", "Utility", MC_SYSTEM,
	  "Noise texture quality, hotkeys and the startup-only keys.",
	  -1, NULL },
	// --- Reference ----------------------------------------------------------
	{ "INI Keys", "INI Keys", MC_REFERENCE,
	  "Full inventory: every key, every row, greys included.",
	  -1, DrawReferencePane },
	{ "All Settings", "All Settings", MC_REFERENCE,
	  "Coverage inventory grouped by subsystem, plus live diagnostics.",
	  -1, DrawAllSettingsTab },
};

static const char *const kCategoryNames[MC__COUNT] = {
	"SETUP", "EFFECTS", "WORLD", "DIAGNOSTICS", "SYSTEM", "REFERENCE"
};

static const int kSectionCount = (int)(sizeof(s_menuSections)/sizeof(s_menuSections[0]));
static int  s_activeSection = 0;	// index into s_menuSections (session UI state)

// ------------------------------------------------------------
// ON/OFF pill - colour-coded state readout for the section list.
// Same pill geometry as DrawStatusBadge (draw-list only, anchored to the
// previous item's line) but the tint is the palette's semantic
// ok/textDisabled pair: green = the effect is running, grey = it is off
// (its values can still be staged).
// rightScreenX > 0 aligns the pill's right edge to that screen coordinate
// and is honoured exactly - both callers anchor it to a full-width band
// (the nav Selectable / the pane title), so unlike DrawStatusBadge there
// is no inline fallback that could push the pill outside the band.
// Lives in this TU (not the theme) because it encodes menu semantics, and
// the theme header needs no edit for it: it is built from the exported
// g_dmPalette / DmColU32 / DmMix helpers alone.
// ------------------------------------------------------------
static const char *const kOnOffText[2] = { "OFF", "ON" };

static void
DrawOnOffPill(bool on, float rightScreenX)
{
	if(ImGui::GetCurrentContext() == NULL)
		return;
	const DmPalette &p = g_dmPalette;
	const char *text = kOnOffText[on ? 1 : 0];
	const ImU32 tint = on ? p.ok : p.textDisabled;
	const ImVec2 ts = ImGui::CalcTextSize(text);
	const float padX = 6.0f;
	const float h = ts.y + 3.0f;
	const float w = ts.x + padX * 2.0f;

	const ImVec2 lastMin = ImGui::GetItemRectMin();
	const ImVec2 lastMax = ImGui::GetItemRectMax();
	const float y = (lastMin.y + lastMax.y) * 0.5f - h * 0.5f;
	const float x = (rightScreenX > 0.0f) ? (rightScreenX - w)
	                                      : (lastMax.x + 8.0f);

	const ImVec2 a(x, y);
	const ImVec2 b(x + w, y + h);
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const float r = h * 0.5f;

	dl->AddRectFilled(a, b, DmColU32(tint, on ? 0.20f : 0.10f), r);
#if IMGUI_VERSION_NUM >= 19208
	dl->AddRect(a, b, DmColU32(tint, on ? 0.75f : 0.45f), r, 1.0f);
#else
	dl->AddRect(a, b, DmColU32(tint, on ? 0.75f : 0.45f), r, 0, 1.0f);
#endif
	dl->AddText(ImVec2(a.x + padX, a.y + (h - ts.y) * 0.5f),
		DmColU32(DmMix(tint, p.textPrimary, 0.34f), 1.0f), text);
}

// Generic pane body for a row-backed section: glass title band (with the
// ON/OFF pill at its right edge when the section has a master switch), the
// section description, the off hint, then the rows.
static void
DrawSectionPane(const MenuSection &s)
{
	const bool on = EffectSwitchOn(s.sw);

	DrawGlassHeader(s.label, ImVec2(0.0f, 28.0f));
	if(s.sw >= 0)
		DrawOnOffPill(on, ImGui::GetItemRectMax().x - 8.0f);
	DrawAccentSeparator();
	if(!on){
		char hint[192];
		snprintf(hint, sizeof(hint),
			"%s is off. The settings below are still editable; switch it on in Switchboard.",
			s.label);
		ImGui::TextDisabled("%s", hint);
		DrawAccentSeparator();
	}
	if(s.desc && s.desc[0])
		ImGui::TextDisabled("%s", s.desc);
	// Per-section child id so the scroll position does not carry over
	// between sections (same rule the old per-tab effect bodies used).
	std::string bodyId = std::string("##secbody_") + s.id;
	ImGui::BeginChild(bodyId.c_str(), ImVec2(0.0f, 0.0f));
	DrawRowsForTab(s.id, s_settingFilter);
	ImGui::EndChild();
}

// LEFT column: category-grouped section list. Selectables are nav-focusable
// (NavEnableKeyboard is set at init): Up/Down move focus, Enter opens. A
// section whose effect is off keeps its label dimmed but stays selectable
// (values can be staged before the effect is on), and its pill reads OFF.
static void
DrawSectionNav(void)
{
	const float navW = 256.0f;
	ImGui::BeginChild("SectionNav", ImVec2(navW, 0.0f), ImGuiChildFlags_Borders);
	// Taller nav rows for the larger window: +2px vertical frame padding on
	// every Selectable gives each section label a bigger, easier hit target.
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
		ImVec2(ImGui::GetStyle().FramePadding.x, ImGui::GetStyle().FramePadding.y + 2.0f));
	ImGui::PushItemWidth(navW - 16.0f);

	int cat = -1;
	for(int i = 0; i < kSectionCount; i++){
		const MenuSection &s = s_menuSections[i];
		if(s.cat != cat){
			cat = s.cat;
			if(i > 0)
				ImGui::Spacing();
			ImGui::TextDisabled("%s", kCategoryNames[cat]);
			ImGui::Separator();
		}
		const bool on = EffectSwitchOn(s.sw);
		const bool sel = (i == s_activeSection);

		char label[96];
		snprintf(label, sizeof(label), "%s###sec_%s", s.label, s.id);
		if(!on)
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		if(ImGui::Selectable(label, sel))
			s_activeSection = i;
		if(!on)
			ImGui::PopStyleColor();

		// Status pill at the selectable's right edge (draw-list only - the
		// selectable itself stays the single hit target, so clicking the
		// pill still opens the section).
		if(s.sw >= 0)
			DrawOnOffPill(on, ImGui::GetItemRectMax().x - 4.0f);

		if(ImGui::IsItemHovered()){
			char tip[256];
			if(s.sw >= 0 && !on)
				snprintf(tip, sizeof(tip), "%s (currently off)\n%s", s.label, s.desc);
			else
				snprintf(tip, sizeof(tip), "%s\n%s", s.label, s.desc);
			ImGui::SetTooltip("%s", tip);
		}
	}
	ImGui::PopItemWidth();
	ImGui::PopStyleVar();
	ImGui::EndChild();
}

// ============================================================
// MAIN PANE + the switchboard (left detail body for "Main")

// Live state of a tab's master switch — the same variable the main-pane
// toggle writes, so the two can never disagree.
static bool
EffectSwitchOn(int sw)
{
	switch(sw){
	case EF_SSAO:          return config->ssaoEnable != 0;
	case EF_MOTION_BLUR:   return config->motionBlurEnable != 0;
	case EF_GOD_RAYS:      return config->godRaysEnable != 0;
	case EF_HEIGHT_FOG:    return config->heightFogEnable != 0;
	case EF_SMAA:          return config->smaaEnable != 0;
	case EF_PIPE_CHAIN:    return config->pipeChainEnable != 0;
	case EF_RADIOSITY:     return config->doRadiosity != 0;
	case EF_COLOUR_FILTER: return config->colorFilterEnable != 0;
	case EF_IV_GRADE:      return config->ivMode != 0;
	case EF_SSS:           return config->sssEnable != 0;
	case EF_VCS_TRAILS:    return config->vcsTrails != 0;
	case EF_DOF:           return config->dofEnable != 0;
	case EF_BLOOM:         return config->bloomEnable != 0;
	case EF_SUN_SHAFTS:    return config->sunShaftsEnable != 0;
	case EF_GRAIN:         return config->grainEnable != 0;
	default:               return true;	// no switch -> always available
	}
}

// Left column of the main pane: the switchboard.
static void
DrawMainPane(void)
{
	DrawGlassHeader("Switchboard", ImVec2(0.0f, 28.0f));
	ImGui::TextDisabled("Switch effects on and off here.\nOpen an EFFECTS section in the list to fine-tune one.");
	// Live readout (was on the old Graphics tab): only interesting while
	// Forward+ is on, and the pane is where its switch lives.
	if(config->forwardPlusEnable){
		extern int g_fpGpuLightCount;
		extern int g_fpNumLights;
		ImGui::TextDisabled("Forward+ lights: %d (collected %d)", g_fpGpuLightCount, g_fpNumLights);
		// Cluster debug viewer. This checkbox writes 0/1; the
		// "Forward+ debug view" row in the Rendering features group also
		// exposes mode 2 (tiles + legend), so the key is not hidden here.
		// forwardplus.cpp reads config->forwardPlusDebug directly
		// (FpUpdateDebugView), so either write lands the same frame.
		// ImGui needs a bool*; RwBool is RwInt32 (rwplcore.h), so the !=0 /
		// ?1:0 staging is exact.
		bool fpDebug = (config->forwardPlusDebug != 0);
		ImGui::Checkbox("Forward+ cluster debug", &fpDebug);
		if(fpDebug != (config->forwardPlusDebug != 0)){
			config->forwardPlusDebug = fpDebug ? 1 : 0;
			refreshIni();
		}
		extern int g_fpStats[6];
		extern void ForwardPlus_StatsText(char *out, int outSize);
		if(g_fpStats){
			char fpLine[320];
			ForwardPlus_StatsText(fpLine, sizeof(fpLine));
			ImGui::TextDisabled("%s", fpLine);
		}
	}
	DrawAccentSeparator();
	DrawRowsForTab("Main", s_settingFilter);
}

// Menu-side re-clamps, run once per frame while the menu is open. They keep
// the sliders from DISPLAYING out-of-range values written by a writer the
// load-time clamp can't see, and they are the menu half of the black-screen
// fix: Min exposure is [-0.5..0.5], Max exposure [0.2..4.0], Min <= Max, and
// the curve stays inside +/-0.35. They match main.cpp's clamp functions.
static void
ClampMenuRanges(void)
{
	// SSAO power is a gamma on occlusion — load/reload/save clamp [0.5..4.0]
	// (9.237 turned mild AO into a hard object-shaped silhouette).
	if(config->ssaoPower < 0.5f || config->ssaoPower > 4.0f){
		if(dbglog_throttle("ssaop_clamp"))
			dbglog("[Menu] ssaoPower=%.3f outside [0.5..4.0], clamping", config->ssaoPower);
		config->ssaoPower = (config->ssaoPower < 0.5f) ? 0.5f : 4.0f;
	}

	// Env reflection sliders are hard-ranged [0..0.4] in readIni/refresh/save.
	if(config->envFresnel < 0.0f || config->envFresnel > 0.4f){
		if(dbglog_throttle("envf_clamp"))
			dbglog("[Menu] envFresnel=%.3f outside [0..0.4], clamping", config->envFresnel);
		config->envFresnel = (config->envFresnel < 0.0f) ? 0.0f : 0.4f;
	}
	if(config->envPower < 1.0f || config->envPower > 128.0f){
		if(dbglog_throttle("envp_clamp"))
			dbglog("[Menu] envPower=%.3f outside [1..128], clamping", config->envPower);
		config->envPower = (config->envPower < 1.0f) ? 1.0f : 128.0f;
	}
	if(config->vehEnvIntensity < 0.0f || config->vehEnvIntensity > 0.4f){
		if(dbglog_throttle("envi_clamp"))
			dbglog("[Menu] vehEnvIntensity=%.3f outside [0..0.4], clamping", config->vehEnvIntensity);
		config->vehEnvIntensity = (config->vehEnvIntensity < 0.0f) ? 0.0f : 0.4f;
	}
	if(config->vehChromeEnvThreshold < 0.0f)
		config->vehChromeEnvThreshold = 0.0f;
	if(config->ambientFloor < 0.0f || config->ambientFloor > 0.2f){
		if(dbglog_throttle("ambfloor_clamp"))
			dbglog("[Menu] ambientFloor=%.3f outside [0..0.2], clamping", config->ambientFloor);
		config->ambientFloor = (config->ambientFloor < 0.0f) ? 0.0f : 0.2f;
	}
	// State-integrity keys are 0/1 in readIniFile.
	struct { int *v; const char *tag; } stateBits[] = {
		{ &config->texAlphaClassify, "texAlphaClassify" },
		{ &config->stateBaselineEnable, "stateBaselineEnable" },
		{ &config->defaultCbShaderHygiene, "defaultCbShaderHygiene" },
		{ &config->pipeChainReflEnable, "pipeChainReflEnable" },
	};
	for(size_t i = 0; i < sizeof(stateBits)/sizeof(stateBits[0]); i++){
		if(*stateBits[i].v < 0 || *stateBits[i].v > 1){
			if(dbglog_throttle("statebit_clamp"))
				dbglog("[Menu] %s=%d outside [0..1], clamping", stateBits[i].tag, *stateBits[i].v);
			*stateBits[i].v = (*stateBits[i].v < 0) ? 0 : 1;
		}
	}
	if(g_waterFresnelSplit < 0 || g_waterFresnelSplit > 1)
		g_waterFresnelSplit = g_waterFresnelSplit < 0 ? 0 : 1;
	// chrome params — ranges must match main.cpp clampChromeParams
	{
		struct { float *v; float lo; float hi; } chromeRanges[] = {
			{ &config->chromeF0,        0.0f, 1.0f },
			{ &config->chromeGloss,     0.0f, 1.0f },
			{ &config->chromeMetallic,  0.0f, 1.0f },
			{ &config->chromeEnvBoost,  0.0f, 4.0f },
			{ &config->chromeClearcoat, 0.0f, 1.0f },
		};
		for(size_t i = 0; i < sizeof(chromeRanges)/sizeof(chromeRanges[0]); i++){
			float *v = chromeRanges[i].v;
			if(*v < chromeRanges[i].lo || *v > chromeRanges[i].hi){
				if(dbglog_throttle("chrome_clamp"))
					dbglog("[Menu] chrome param=%.3f outside [%.1f..%.1f], clamping",
						*v, chromeRanges[i].lo, chromeRanges[i].hi);
				*v = (*v < chromeRanges[i].lo) ? chromeRanges[i].lo : chromeRanges[i].hi;
			}
		}
	}

	// Tonemap exposure window — match main.cpp clampTonemapExposureRanges:
	// Min [-0.5..0.5], Max [0.2..4.0]; Min <= Max is enforced by pinning
	// min = max. (The old menu clamp [0.2..4.0] for Min disagreed with the
	// parser and hid the legal negative range for deliberate darkening.)
	// Curve intensities stay within +/-0.35 where PivotCurve is a gentle gain
	// ramp (readIni/refresh/save + the postfx upload assert the same bounds).
	if(config->tonemapMinExposure < -0.5f || config->tonemapMinExposure > 0.5f){
		if(dbglog_throttle("tmin_clamp"))
			dbglog("[Menu] tonemapMinExposure=%.3f outside [-0.5..0.5], clamping", config->tonemapMinExposure);
		config->tonemapMinExposure = (config->tonemapMinExposure < -0.5f) ? -0.5f : 0.5f;
	}
	if(config->tonemapMaxExposure < 0.2f || config->tonemapMaxExposure > 4.0f){
		if(dbglog_throttle("tmax_clamp"))
			dbglog("[Menu] tonemapMaxExposure=%.3f outside [0.2..4.0], clamping", config->tonemapMaxExposure);
		config->tonemapMaxExposure = (config->tonemapMaxExposure < 0.2f) ? 0.2f : 4.0f;
	}
	if(config->tonemapMinExposure > config->tonemapMaxExposure){
		if(dbglog_throttle("texp_order"))
			dbglog("[Menu] tonemapMin>Max (%.3f > %.3f), pinning min = max",
				config->tonemapMinExposure, config->tonemapMaxExposure);
		config->tonemapMinExposure = config->tonemapMaxExposure;
	}
	if(config->tonemapCurveLows < -0.35f || config->tonemapCurveLows > 0.35f){
		if(dbglog_throttle("tclow_clamp"))
			dbglog("[Menu] tonemapCurveLows=%.3f outside [-0.35..0.35], clamping", config->tonemapCurveLows);
		config->tonemapCurveLows = (config->tonemapCurveLows < -0.35f) ? -0.35f : 0.35f;
	}
	if(config->tonemapCurveHighs < -0.35f || config->tonemapCurveHighs > 0.35f){
		if(dbglog_throttle("tchig_clamp"))
			dbglog("[Menu] tonemapCurveHighs=%.3f outside [-0.35..0.35], clamping", config->tonemapCurveHighs);
		config->tonemapCurveHighs = (config->tonemapCurveHighs < -0.35f) ? -0.35f : 0.35f;
	}
	if(config->tonemapCurveMid < 0.0f || config->tonemapCurveMid > 1.0f){
		if(dbglog_throttle("tcmid_clamp"))
			dbglog("[Menu] tonemapCurveMid=%.3f outside [0..1], clamping", config->tonemapCurveMid);
		config->tonemapCurveMid = (config->tonemapCurveMid < 0.0f) ? 0.0f : 1.0f;
	}
}

// ============================================================
// TAB: PROFILER — live PF_SCOPE readout (src/render/profiler.cpp)
//   the two switch rows (BuildSettingRows, tab "Profiler") then a live
//   frame-time sparkline, a per-scope CPU/GPU table and the note ring.
//   Everything is read from the profiler's snapshot accessors, so this tab
//   never touches the tables the instrumented code writes.
// ============================================================
static void
DrawProfilerTab(void)
{
	static const int kMaxScopes = 32;	// matches PF_MAX_SLOTS in profiler.cpp
	static const int kMaxBinds  = 48;	// matches PF_MAX_BINDS
	static const int kNoteBuf   = 8*80;

	ImGui::BeginChild("profrows", ImVec2(0.0f, 0.0f));

	// --- switches (same rows as any other tab) -----------------------------
	DrawRowsForTab("Profiler", s_settingFilter);

	// --- frame summary -----------------------------------------------------
	float ms = 0.0f, mn = 0.0f, avg = 0.0f, mx = 0.0f;
	long long draws = 0, verts = 0, tris = 0;
	int psBinds = 0, vsBinds = 0;
	PfFrameInfo(&ms, &mn, &avg, &mx, &draws, &verts, &tris, &psBinds, &vsBinds);

	int histN = 0;
	const float *hist = PfFrameHistory(&histN);

	DrawGlassHeader("Frame budget", ImVec2(0.0f, 24.0f));
	ImGui::Text("Frame  %7.3f ms      min %7.3f      avg %7.3f      max %7.3f", ms, mn, avg, mx);
	if(histN > 1){
		// Ceiling from the peak (never 0 -> no divide-by-zero, no flat line)
		char overlay[48];
		snprintf(overlay, sizeof(overlay), "frame ms (last %d)", histN);
		float scale = (mx > 0.0f) ? mx * 1.25f : 16.0f;
		ImGui::PlotLines("##pf_frame_hist", hist, histN, 0, overlay, 0.0f, scale,
			ImVec2(0.0f, 64.0f));
	}
	ImGui::Text("Draws %lld     verts %lld     tris %lld     ps binds %d     vs binds %d",
		draws, verts, tris, psBinds, vsBinds);

	if(ImGui::Button("Reset profiler stats"))
		PfResetStats();
	ImGui::SameLine();
	ImGui::TextDisabled("(min/avg/max restart from the next frame)");

	// --- per-scope table ---------------------------------------------------
	const char *names[kMaxScopes];
	float cpuMs[kMaxScopes], cpuMin[kMaxScopes], cpuAvg[kMaxScopes], cpuMax[kMaxScopes], gpuMs[kMaxScopes];
	int n = PfSnapshotNames(names, cpuMs, cpuMin, cpuAvg, cpuMax, gpuMs, kMaxScopes);

	DrawGlassHeader("Scopes (PF_SCOPE)", ImVec2(0.0f, 24.0f));
	if(n == 0){
		ImGui::TextDisabled("(nothing instrumented yet - wrap a pass with PF_SCOPE(\"Name\"))");
	}else if(ImGui::BeginTable("pf_scopes", 7,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)){
		ImGui::TableSetupColumn("scope",      ImGuiTableColumnFlags_WidthFixed, 170.0f);
		ImGui::TableSetupColumn("cpu ms",     ImGuiTableColumnFlags_WidthFixed,  72.0f);
		ImGui::TableSetupColumn("gpu ms",     ImGuiTableColumnFlags_WidthFixed,  72.0f);
		ImGui::TableSetupColumn("min",        ImGuiTableColumnFlags_WidthFixed,  66.0f);
		ImGui::TableSetupColumn("avg",        ImGuiTableColumnFlags_WidthFixed,  66.0f);
		ImGui::TableSetupColumn("max",        ImGuiTableColumnFlags_WidthFixed,  66.0f);
		ImGui::TableSetupColumn("% frame",    ImGuiTableColumnFlags_WidthFixed,  66.0f);
		ImGui::TableHeadersRow();
		for(int i = 0; i < n; i++){
			float pct = (ms > 1e-7f) ? (cpuMs[i] * 100.0f / ms) : 0.0f;
			ImGui::TableNextRow();
			ImGui::TableNextColumn(); ImGui::TextUnformatted(names[i]);
			ImGui::TableNextColumn(); ImGui::Text("%7.3f", cpuMs[i]);
			ImGui::TableNextColumn(); ImGui::Text("%7.3f", gpuMs[i]);
			ImGui::TableNextColumn(); ImGui::Text("%7.3f", cpuMin[i]);
			ImGui::TableNextColumn(); ImGui::Text("%7.3f", cpuAvg[i]);
			ImGui::TableNextColumn(); ImGui::Text("%7.3f", cpuMax[i]);
			ImGui::TableNextColumn(); ImGui::Text("%5.1f%%", pct);
		}
		ImGui::EndTable();
	}

	// --- shader bind table --------------------------------------------------
	const char *btags[kMaxBinds];
	void *lps[kMaxBinds], *lvs[kMaxBinds];
	int bps[kMaxBinds], bvs[kMaxBinds];
	int bn = PfBindSnapshot(btags, lps, lvs, bps, bvs, kMaxBinds);

	DrawGlassHeader("Shader binds (last frame)", ImVec2(0.0f, 24.0f));
	if(bn == 0){
		ImGui::TextDisabled("(none - PfBindPS/PfBindVS are not called yet)");
	}else if(ImGui::BeginTable("pf_binds", 4,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)){
		ImGui::TableSetupColumn("tag",  ImGuiTableColumnFlags_WidthFixed, 170.0f);
		ImGui::TableSetupColumn("ps",   ImGuiTableColumnFlags_WidthFixed,  72.0f);
		ImGui::TableSetupColumn("vs",   ImGuiTableColumnFlags_WidthFixed,  72.0f);
		ImGui::TableSetupColumn("last ps / vs", ImGuiTableColumnFlags_WidthStretch, 200.0f);
		ImGui::TableHeadersRow();
		for(int i = 0; i < bn; i++){
			ImGui::TableNextRow();
			ImGui::TableNextColumn(); ImGui::TextUnformatted(btags[i]);
			ImGui::TableNextColumn(); ImGui::Text("%d", bps[i]);
			ImGui::TableNextColumn(); ImGui::Text("%d", bvs[i]);
			ImGui::TableNextColumn(); ImGui::Text("%p / %p", lps[i], lvs[i]);
		}
		ImGui::EndTable();
	}

	// --- note ring ----------------------------------------------------------
	static char s_pfNotes[kNoteBuf];
	PfNotesSnapshot(s_pfNotes, sizeof(s_pfNotes));
	DrawGlassHeader("Notes (PfNote ring, newest first)", ImVec2(0.0f, 24.0f));
	ImGui::TextDisabled("%s", (s_pfNotes[0] != '\0') ? s_pfNotes : "(none)");

	ImGui::EndChild();
}

// ============================================================
// EFFECTIVE-VALUE SUMMARY
//   The rows below are the stored values. These lines explain what the
//   renderer actually resolves them to when a runtime writer (PBR filter
//   lock, mode manager, IV family sync, preset) disagrees with the raw
//   Config field.
// ============================================================
static const char *
PipeName(int v)
{
	return (v >= 0 && v < NUMPIPELINES) ? s_pipeNames[v] : "?";
}

static const char *
BuildName(int v)
{
	return (v >= 0 && v < NUMBUILDINGPIPES) ? s_buildNames[v] : "?";
}

static const char *
VehName(int v)
{
	return (v >= 0 && v < NUMCARPIPES) ? s_vehNames[v] : "?";
}

static const char *
FilterName(int v)
{
	return (v >= 0 && v <= COLORFILTER_MODERN) ? s_cfNames[v] : "?";
}

static const char *
WaterName(int v)
{
	return (v >= 0 && v < 3) ? s_waterSty[v] : "?";
}

static const char *
PresetName(int v)
{
	return (v >= 0 && v < NUM_PRESETS && presetNames[v]) ? presetNames[v] : "Custom";
}

static void
DrawEffectiveSummary(void)
{
	if(!config)
		return;
	int derivedFilter = (config->pipeline == PIPELINE_PBR || config->buildingPipe == BUILDING_PBR)
		? COLORFILTER_MODERN : config->colorFilter;
	ImGui::TextDisabled("Effective: pipeline=%s | building=%s | vehicle=%s | filter=%s | water=%s",
		PipeName(config->pipeline), BuildName(config->buildingPipe), VehName(config->vehiclePipe),
		FilterName(derivedFilter), WaterName(config->waterStyle));
	if((config->pipeline == PIPELINE_PBR || config->buildingPipe == BUILDING_PBR) &&
	   config->colorFilter != COLORFILTER_MODERN)
		ImGui::TextDisabled("  Colour filter: PBR forces Modern (stored value is overridden every frame).");
	if(config->preset != PRESET_CUSTOM)
		ImGui::TextDisabled("  Stored gamePreset: %s (%d). Presets are applied at load / by the Style combo;"
			" they do not continuously override the controls below.", PresetName(config->preset), config->preset);
	if(PipeFamilyIsIV() && !config->ivMode)
		ImGui::TextDisabled("  IV pipe family is active but the IV grade is OFF (explicit ivMode=0).");
	if(!PipeFamilyIsIV() && config->ivMode)
		ImGui::TextDisabled("  IV grade is ON over a non-IV pipe family (explicit ivMode=1).");
	ImGui::TextDisabled("  Colour filter pass: %s | Pipe chain: %s (reflection %s) | IV mode: %d",
		config->colorFilterEnable ? "on" : "bypassed",
		config->pipeChainEnable ? "on" : "off",
		config->pipeChainReflEnable ? "on" : "off",
		(int)config->ivMode);
	ImGui::TextDisabled("  IV pipe family: %s (ivMode auto-syncs only when a pipe edit crosses the family boundary)",
		PipeFamilyIsIV() ? "yes" : "no");
	if(config->pipelineOverride >= 0)
		ImGui::TextDisabled("  pipelineOverride=%d is active; pipeline is re-derived from the pipes after every edit.",
			config->pipelineOverride);
}

// ============================================================
// INI CLIPBOARD + SAVE COVERAGE
//   saveConfigTo already writes the Config keys. A handful of menu-editable
//   keys live outside Config (waterFresnelSplit, cloudRenderFix) or are
//   startup-only globals. The menu writes those back on Save INI / Save as
//   New Config, and the Copy action renders the exact current values in INI
//   syntax for a bug report without touching disk.
// ============================================================
static bool
KeyUsesStrAssoc(const char *key)
{
	return !strcmp(key, "pipeline") || !strcmp(key, "buildingPipe") ||
	       !strcmp(key, "vehiclePipe") || !strcmp(key, "colorFilter") ||
	       !strcmp(key, "waterStyle") || !strcmp(key, "radiosity") ||
	       !strcmp(key, "infraredVision") || !strcmp(key, "nightVision") ||
	       !strcmp(key, "grainFilter");
}

static bool
RowIniValue(const SettingRow &r, char *buf, int n)
{
	switch(r.kind){
	case RK_BOOL:   snprintf(buf, n, "%d", (*(int*)r.ptr) ? 1 : 0); return true;
	case RK_BOOL8:  snprintf(buf, n, "%d", (*(bool*)r.ptr) ? 1 : 0); return true;
	case RK_INT:
		if(!strcmp(r.key, "keySwitch") || !strcmp(r.key, "keyReload"))
			snprintf(buf, n, "0x%X", *(int*)r.ptr);
		else
			snprintf(buf, n, "%d", *(int*)r.ptr);
		return true;
	case RK_FLOAT:  snprintf(buf, n, "%.6g", (double)*(float*)r.ptr); return true;
	case RK_ENUM:{
		int v = *(int*)r.ptr;
		if(KeyUsesStrAssoc(r.key) && r.names){
			int i = v - r.base;
			const char *nm = EnumNameAt(r.names, i);
			if(nm){ snprintf(buf, n, "%s", nm); return true; }
		}
		snprintf(buf, n, "%d", v);
		return true;}
	case RK_STATIC:
		if(r.ptr){ snprintf(buf, n, "%d", *(int*)r.ptr); return true; }
		return false;
	case RK_STATICB:
		if(r.ptr){ snprintf(buf, n, "%d", (*(bool*)r.ptr) ? 1 : 0); return true; }
		return false;
	case RK_NOISEQ:
		snprintf(buf, n, "%d", menuGetNoiseQuality());
		return true;
	case RK_INIINT:
		snprintf(buf, n, "%d", ReadCurrentIniInt(r.key, 1));
		return true;
	default:
		return false;
	}
}

static void
BuildIniClipboard(std::string &out, int *count)
{
	out.clear();
	if(count)
		*count = 0;
	std::vector<SettingRow> &rows = GetSettingRows();
	const char *sections[] = { "SkyGfx", "General" };
	for(int si = 0; si < 2; si++){
		bool header = false;
		std::vector<std::string> seen;
		for(size_t i = 0; i < rows.size(); i++){
			const SettingRow &r = rows[i];
			if(strcmp(RowSection(r), sections[si]) != 0)
				continue;
			if(strcmp(r.key, "ps2grassFiles") == 0)
				continue;
			bool dup = false;
			for(size_t j = 0; j < seen.size(); j++)
				if(seen[j] == r.key){ dup = true; break; }
			if(dup)
				continue;
			seen.push_back(r.key);
			char val[128];
			if(!RowIniValue(r, val, sizeof(val)))
				continue;
			if(!header){
				if(!out.empty())
					out += "\n";
				out += "[";
				out += sections[si];
				out += "]\n";
				header = true;
			}
			out += r.key;
			out += "=";
			out += val;
			out += "\n";
			if(count)
				(*count)++;
		}
	}
}

static void
CopySettingsAsIni(void)
{
	std::string out;
	int n = 0;
	BuildIniClipboard(out, &n);
	ImGui::SetClipboardText(out.c_str());
	snprintf(s_copyStatus, sizeof(s_copyStatus), "Copied %d keys to the clipboard.", n);
	s_copyStatusTime = ImGui::GetTime();
	if(dbglog_throttle("ini_copy"))
		dbglog("[Menu] copied %d current settings as INI", n);
}

// saveConfigTo() omits these keys: either they live outside Config
// (waterFresnelSplit / cloudRenderFix) or they are startup globals read by
// main.cpp's patch installers. Write them next to the normal save so the
// menu's Save buttons cover what the menu can edit.
static void
MenuWriteMissingIniKeys(const char *path)
{
	if(!path || !path[0] || !config)
		return;
	if(GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
		return;	// saveConfigTo failed / file absent: never create a partial INI here
	WritePrivateProfileStringA("SkyGfx", "waterFresnelSplit",
		std::to_string(g_waterFresnelSplit).c_str(), path);
	WritePrivateProfileStringA("SkyGfx", "cloudRenderFix",
		cloudRenderFix ? "1" : "0", path);
	char tmp[32];
	snprintf(tmp, sizeof(tmp), "0x%X", config->keys[0]);
	WritePrivateProfileStringA("SkyGfx", "keySwitch", tmp, path);
	snprintf(tmp, sizeof(tmp), "0x%X", config->keys[1]);
	WritePrivateProfileStringA("SkyGfx", "keyReload", tmp, path);
	WritePrivateProfileStringA("SkyGfx", "disableClouds", disableClouds ? "1" : "0", path);
	WritePrivateProfileStringA("SkyGfx", "disableGamma", disableGamma ? "1" : "0", path);
	WritePrivateProfileStringA("SkyGfx", "fixPcCarLight", fixPcCarLight ? "1" : "0", path);
	WritePrivateProfileStringA("SkyGfx", "privateHooks", privateHooks ? "1" : "0", path);
	WritePrivateProfileStringA("SkyGfx", "forceWindShader", forceWindShader ? "1" : "0", path);
	WritePrivateProfileStringA("SkyGfx", "transparentLockon", std::to_string(transparentLockon).c_str(), path);
	WritePrivateProfileStringA("SkyGfx", "fixShadows", std::to_string(fixShadows).c_str(), path);
	WritePrivateProfileStringA("SkyGfx", "explicitBuildingPipe", std::to_string(explicitBuildingPipe_tmp).c_str(), path);
	// Diagnostics rows: diagLevel is parsed by readIniFile but saveConfigTo
	// omits it (menu edits would otherwise revert on reload); debugOverlay
	// is read once by the overlay's own init and has no Config field.
	WritePrivateProfileStringA("SkyGfx", "diagLevel", std::to_string(config->diagLevel).c_str(), path);
	WritePrivateProfileStringA("SkyGfx", "debugOverlay", s_ovEnabled ? "1" : "0", path);
	if(dbglog_throttle("menu_ini_extra"))
		dbglog("[Menu] save: wrote menu-only INI keys to %s", path);
}

static void
DrawLiveDiagnostics(void)
{
	if(!DrawGlassCollapsingHeader("Live diagnostics (read-only)", true))
		return;
	DrawAccentSeparator();
	if(!config){
		ImGui::TextDisabled("config pointer is null");
		return;
	}

	DrawEffectiveSummary();
	DrawAccentSeparator();
	ImGui::TextUnformatted("Subsystem state");
	ImGui::TextDisabled("pipeline=%d buildingPipe=%d vehiclePipe=%d colorFilter=%d (effective %d) colorFilterEnable=%d",
		config->pipeline, config->buildingPipe, config->vehiclePipe, config->colorFilter,
		(config->pipeline == PIPELINE_PBR || config->buildingPipe == BUILDING_PBR)
			? COLORFILTER_MODERN : config->colorFilter,
		(int)config->colorFilterEnable);
	ImGui::TextDisabled("modeManager=%d | ivMode=%d | preset=%d | configs=%d current=%d",
		(int)ModeManager_GetMode(), (int)config->ivMode, config->preset, numConfigs, currentConfig);
	ImGui::TextDisabled("state: texAlphaClassify=%d stateBaseline=%d defaultCbHygiene=%d cloudRenderFix=%d",
		config->texAlphaClassify, config->stateBaselineEnable, config->defaultCbShaderHygiene,
		cloudRenderFix ? 1 : 0);
	ImGui::TextDisabled("coronaZtest=%d | sunGlare key=%d (capable=%d) | neonDrops capable=%d",
		config->coronaZtest, (int)config->doglare, iCanHasSunGlare ? 1 : 0,
		iCanHasNeoDrops ? 1 : 0);
	ImGui::TextDisabled("pipeChain: enable=%d reflection=%d intensity=%.3f | pipeChainReflEnable is the postfx kill-switch",
		(int)config->pipeChainEnable, config->pipeChainReflEnable, config->pipeChainIntensity);
	ImGui::TextDisabled("normalMap: enable=%d intensity=%.3f playerOnly=%d plugin=%d",
		(int)config->normalMapEnable, config->normalMapIntensity,
		(int)config->normalMapPlayerOnly, gHasExternalNormalMapPlugin ? 1 : 0);
	ImGui::TextDisabled("normalBuffer: enable=%d tex=%p | velocity=%d depthHook=%d intzTex=%p",
		(int)config->normalBufferEnable, (void*)g_normalBufferTex,
		(int)config->velocityBufferEnable, (int)config->depthHookEnable, (void*)g_intzTex);
	ImGui::TextDisabled("water: pipe=%d parallaxActive=%d style=%s quality=%d useTimecycle=%d fresnelSplit=%d",
		(int)config->waterParallaxEnable, g_waterParallaxActive ? 1 : 0,
		WaterName(config->waterStyle), config->waterQuality,
		(int)config->waterUseTimecycle, g_waterFresnelSplit);
	ImGui::TextDisabled("forwardPlus: enable=%d debug=%d fallback(INI)=%d | lights=%d gpu=%d",
		(int)config->forwardPlusEnable, (int)config->forwardPlusDebug,
		ReadCurrentIniInt("forwardPlusFallback", 1), g_fpNumLights, g_fpGpuLightCount);
	{
		extern int g_fpStats[6];
		extern void ForwardPlus_StatsText(char *out, int outSize);
		if(g_fpStats){
			char fpLine[320];
			ForwardPlus_StatsText(fpLine, sizeof(fpLine));
			ImGui::TextDisabled("%s", fpLine);
		}
	}
	ImGui::TextDisabled("profiler: enable=%d gpuTimers=%d | envmap reflectionTex=%p size=%d",
		(int)g_pfEnable, (int)g_pfGpuTimers, (void*)reflectionTex, config->envMapSize);
	ImGui::TextDisabled("INI: %s",
		CurrentIniPathString().c_str());
	ImGui::TextDisabled("d3d9device=%p | iblTex=%p | intzSurf=%p",
		(void*)d3d9device, (void*)g_iblTex, (void*)g_intzSurf);

	float ms = 0.0f, mn = 0.0f, avg = 0.0f, mx = 0.0f;
	long long draws = 0, verts = 0, tris = 0;
	int psBinds = 0, vsBinds = 0;
	PfFrameInfo(&ms, &mn, &avg, &mx, &draws, &verts, &tris, &psBinds, &vsBinds);
	ImGui::TextDisabled("frame %.3f ms (min %.3f avg %.3f max %.3f) draws=%lld verts=%lld tris=%lld psBinds=%d vsBinds=%d",
		ms, mn, avg, mx, draws, verts, tris, psBinds, vsBinds);

	static char notes[8*80];
	PfNotesSnapshot(notes, sizeof(notes));
	ImGui::TextDisabled("Profiler notes (newest first): %s", notes[0] ? notes : "(none)");
	if(ImGui::IsItemHovered())
		ImGui::SetTooltip("PfNote ring from src/render/profiler.cpp. Use it as the nearest\n"
			"thing to a last-log-tag feed; the full dbglog file is skygfx_dbg.log.");
}

static void
DrawAllSettingsTab(void)
{
	std::vector<SettingRow> &rows = GetCoverageRows();
	int shown = 0, ungrouped = 0, groups = 0;
	const char *prevGroup = NULL;
	for(size_t i = 0; i < rows.size(); i++){
		if(RowMatches(rows[i], s_settingFilter))
			shown++;
		if(rows[i].grp && strcmp(rows[i].grp, "Other") == 0)
			ungrouped++;
		if(rows[i].grp && (!prevGroup || strcmp(prevGroup, rows[i].grp) != 0)){
			groups++;
			prevGroup = rows[i].grp;
		}
	}
	DrawGlassHeader("All Settings  //  coverage inventory", ImVec2(0.0f, 28.0f));
	ImGui::TextDisabled("%d unique keys across %d subsystem groups. %d shown with the current filter.",
		(int)rows.size(), groups, shown);
	ImGui::TextDisabled("Every row is the same control as its home tab; reset uses the readIniFile default.");
	if(ungrouped)
		ImGui::TextDisabled("%d key(s) have no subsystem group yet (listed under Other).", ungrouped);
	DrawAccentSeparator();
	ImGui::BeginChild("coverageRows", ImVec2(0, 0));
	DrawLiveDiagnostics();
	DrawAccentSeparator();
	DrawRowsFrom(rows, "All Settings", s_settingFilter);
	ImGui::EndChild();
}

// ============================================================
// CUSTOM PANE WRAPPERS - the sections whose body is not a plain
// DrawRowsForTab call. Each draws its own glass title band, so the
// master-detail dispatcher (DrawUnifiedDebugMenu) only switches on the
// registry entry. The old measured top tab strip (kTopTabs /
// BeginTopTabStrip / TopTabItem) is gone with the strip itself: every
// former top-level tab is now a MenuSection in s_menuSections.
// ============================================================

// Presets pane: the five pipeline presets (pipe combos reachable).
static void
DrawPresetsPane(void)
{
	DrawGlassHeader("Presets  //  modes and pipe sets", ImVec2(0.0f, 28.0f));
	DrawAccentSeparator();
	DrawPresetsTab();
}

// Profiler pane: live pass/frame timings, draw counters, shader binds.
// Instrumentation lives in src/render/profiler.cpp (PF_SCOPE); this pane
// only reads its snapshot accessors.
static void
DrawProfilerPane(void)
{
	DrawGlassHeader("Profiler  //  live timings", ImVec2(0.0f, 28.0f));
	DrawAccentSeparator();
	DrawProfilerTab();
}

// INI Keys pane: full inventory, greys included, filterable.
static void
DrawReferencePane(void)
{
	DrawGlassHeader("INI Keys  //  full inventory", ImVec2(0.0f, 28.0f));
	DrawAccentSeparator();
	DrawReferenceTab();
}

// ============================================================
// MODE CHIPS — always-visible mode selector above the detail area.
//
// The eight picker modes (the canonical 6 + Custom + Mobile). A click
// calls ModeManager_SetMode(id). Clicking the LIT chip passes the active
// id again, which ApplyMode treats as toggle-off and restores the
// per-slot pre-mode Config. The lit chip is read live from
// ModeManager_GetMode() every frame - nothing about the mode is cached.
//
// While a mode is engaged the row also grows an explicit "Restore INI"
// button (ModeManager_Disengage + resetValues, window stays open) and the
// strip below it states the mode, the number of fields it owns and the
// way back. Everything is drawn from the live ModeManager read-only
// introspection, so no mode state is cached here.
//
// Post-change refresh: resetValues(), the same call the Presets tab's
// Modes rows run after ModeManager_SetMode(). ApplyMode already calls it
// internally; the menu repeats it so the CPostEffects copies are current
// before the rest of this frame draws. derivePipelineFromPipes() later in
// DrawUnifiedDebugMenu covers the pipe mirror.
// ============================================================
struct ModeChipDef
{
	ModeId      id;
	const char *label;	// chip text
	const char *tip;	// plain-language summary
};

static const ModeChipDef kModeChips[] = {
	{ MODE_PS2,        "PS2",    "Vintage PS2 pipelines, PS2 colour filter, no postfx" },
	{ MODE_XBOX,       "Xbox",   "Original Xbox pipelines + detail maps" },
	{ MODE_PC_VINTAGE, "PC",     "Fixed PC: Xbox building pipeline + PC car pipe, grain on" },
	{ MODE_MOBILE,     "Mobile", "Mobile / PSP-era pipeline" },
	{ MODE_GTAIV,      "GTA IV", "GTA IV: IV pipes, IV grade, IV postfx" },
	{ MODE_GTAV,       "GTA V",  "GTA V: GTA V water" },
	{ MODE_MODERN,     "Modern", "Modern: PBR + full postfx" },
	{ MODE_CUSTOM,     "Custom", "Custom: your own settings" },
};

// Canonical mode names (mirrors ModeProfile::name in modeManager.cpp,
// which is file-static there). ASCII only: the menu uses ImGui's default
// font, so no typographic dashes or non-Latin glyphs in runtime strings.
static const char *
ModeUiName(ModeId id)
{
	switch(id){
	case MODE_PS2:        return "PS2";
	case MODE_XBOX:       return "Xbox";
	case MODE_PC_VINTAGE: return "Legacy PC";
	case MODE_GTAIV:      return "GTA IV";
	case MODE_GTAV:       return "GTA V";
	case MODE_MODERN:     return "Modern";
	case MODE_CUSTOM:     return "Custom";
	case MODE_MOBILE:     return "Mobile";
	default:              return "None";
	}
}

// Every selectable ModeId now has a chip. The helper stays so the state
// line degrades gracefully if an id outside this table ever becomes active.
static bool
ModeHasChip(ModeId id)
{
	for(int i = 0; i < (int)(sizeof(kModeChips)/sizeof(kModeChips[0])); i++)
		if(kModeChips[i].id == id)
			return true;
	return false;
}

// The chip's own label, so the banner names the mode exactly like the lit
// chip 20px above it. NULL for NONE and for any id without a chip.
static const char *
ModeChipLabel(ModeId id)
{
	for(int i = 0; i < (int)(sizeof(kModeChips)/sizeof(kModeChips[0])); i++)
		if(kModeChips[i].id == id)
			return kModeChips[i].label;
	return NULL;
}

// ASCII uppercase into a caller buffer. The banner reads like a status
// readout and the default menu font has no small-caps face: no allocation,
// no second font size.
static void
ModeUpper(char *dst, int n, const char *src)
{
	if(!dst || n <= 0)
		return;
	int i = 0;
	if(src)
		for(; src[i] && i < n - 1; i++)
			dst[i] = (char)toupper((unsigned char)src[i]);
	dst[i] = '\0';
}

static void
DrawModeChipRow(void)
{
	if(!config)
		return;

	const ImGuiStyle &st = ImGui::GetStyle();
	const int n = (int)(sizeof(kModeChips)/sizeof(kModeChips[0]));
	const ImVec2 start = ImGui::GetCursorScreenPos();
	const float avail = ImGui::GetContentRegionAvail().x;
	const float gap = 6.0f;
	const float h = ImGui::GetTextLineHeight() + 9.0f;

	// RESTORE INI sits immediately after the last chip behind a 1px rule.
	// Measured up front so the chip row gives up padding instead of letting
	// the action run off the right edge on a narrow menu.
	static const char *const kRestoreLabel = "Restore INI";
	static const char *const kRestoreTip =
		"Only the fields the active mode owns go back to the values stored in skygfx.ini.\n"
		"Every other setting is left untouched, and this window stays open.";
	const float restoreW = ImGui::CalcTextSize(kRestoreLabel).x + st.FramePadding.x * 2.0f;
	const float restoreGap = 24.0f;
	const float reserve = (ModeManager_GetMode() != MODE_NONE)
		? restoreW + restoreGap + 8.0f : 0.0f;

	// Chip padding gives way before anything clips. The measured top tab
	// strip below keeps its own padding and metrics untouched.
	float padX = 13.0f;
	float rowW = 0.0f;
	for(;;){
		rowW = gap * (float)(n - 1);
		for(int i = 0; i < n; i++)
			rowW += ImGui::CalcTextSize(kModeChips[i].label).x + padX * 2.0f;
		if(rowW + reserve <= avail || padX <= 5.0f)
			break;
		padX -= 1.0f;
	}

	ModeId cur = ModeManager_GetMode();
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const float r = g_dmMetrics.frameRounding;
	float x = start.x;

	for(int i = 0; i < n; i++){
		const ModeChipDef &c = kModeChips[i];
		const ImVec2 ts = ImGui::CalcTextSize(c.label);
		const float w = ts.x + padX * 2.0f;
		const bool on = (cur == c.id);

		ImGui::SetCursorScreenPos(ImVec2(x, start.y));
		ImGui::PushID((int)c.id);
		// EnableNav: chips are reachable with Tab / activated with Enter or
		// Space; ImGuiCol_NavCursor (accent) draws the focus cue.
		const bool pressed = ImGui::InvisibleButton("chip", ImVec2(w, h),
			ImGuiButtonFlags_EnableNav);
		const bool hovered = ImGui::IsItemHovered();
		ImGui::PopID();

		const ImVec2 a(x, start.y);
		const ImVec2 b(x + w, start.y + h);
		const ImVec2 tp(a.x + (w - ts.x) * 0.5f, a.y + (h - ts.y) * 0.5f);

		if(on){
			// Accent fill plus a 2px halo: the one saturated state in the row.
			// Hover brightens to accentHover so the toggle-off click is visible.
			dl->AddRectFilled(ImVec2(a.x - 2.0f, a.y - 2.0f), ImVec2(b.x + 2.0f, b.y + 2.0f),
				DmColU32(g_dmPalette.accent, 0.12f), r + 2.0f);
			dl->AddRectFilled(a, b,
				DmColU32(hovered ? g_dmPalette.accentHover : g_dmPalette.accent, 1.0f), r);
			dl->AddRect(a, b, DmColU32(g_dmPalette.accentHover, 1.0f), r, 1.0f);
			// bgDeep on accent keeps the label using the existing palette's
			// dark-on-yellow contrast instead of adding a text colour.
			dl->AddText(tp, DmColU32(g_dmPalette.bgDeep, 1.0f), c.label);
		}else{
			dl->AddRectFilled(a, b,
				DmColU32(hovered ? g_dmPalette.bgHover : g_dmPalette.bgRaised, 1.0f), r);
			dl->AddRect(a, b,
				DmColU32(hovered ? g_dmPalette.borderLight : g_dmPalette.border, 1.0f),
				r, 1.0f);
			dl->AddText(tp,
				DmColU32(hovered ? g_dmPalette.textPrimary : g_dmPalette.textSecondary, 1.0f),
				c.label);
		}

		if(hovered)
			ImGui::SetTooltip("%s", c.tip);

		if(pressed){
			ModeKeepMenuOpen keepMenu;	// UI flag must survive the Config swap
			ModeManager_SetMode(c.id);	// same id = toggle off, restores the pre-mode base
			if(dbglog_throttle("mode_click"))
				dbglog("[ModeUI] click %s -> mode=%d menuOpen=%d", c.label,
					(int)ModeManager_GetMode(), (int)config->debugMenuOpen);
			PfNote("mode -> %s", c.label);
			resetValues();			// same refresh as the Presets tab's Modes rows
		}

		x += w + gap;
	}

	// A chip click may have engaged or disengaged this frame: re-read the
	// mode so the action and the banner never show a one-frame lie.
	cur = ModeManager_GetMode();
	bool active = (cur != MODE_NONE);
	int owned = active ? ModeManager_GetOwnedCount(cur) : 0;

	// --- RESTORE INI: visible only while a mode is engaged --------------
	// Explicit, lossless escape hatch: Disengage restores only the fields
	// the profile owns, so this never touches a setting the mode left alone.
	if(active){
		const float btnX = start.x + rowW + restoreGap;
		dl->AddLine(ImVec2(btnX - 13.0f, start.y + 4.0f),
			ImVec2(btnX - 13.0f, start.y + h - 4.0f),
			DmColU32(g_dmPalette.border, 0.90f), 1.0f);
		ImGui::SetCursorScreenPos(ImVec2(btnX, start.y));
		ImGui::PushStyleColor(ImGuiCol_Button, DmCol(g_dmPalette.bgRaised, 0.95f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, DmCol(g_dmPalette.bgHover, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, DmCol(g_dmPalette.bgActive, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_Border, DmCol(g_dmPalette.accentDim, 0.85f));
		ImGui::PushStyleColor(ImGuiCol_Text, DmCol(g_dmPalette.accent));
		const bool restore = ImGui::Button(kRestoreLabel, ImVec2(0.0f, h));
		ImGui::PopStyleColor(5);
		if(ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", kRestoreTip);
		if(restore){
			const ModeId was = cur;
			const int wasOwned = owned;
			const char *wasAlias = ModeManager_GetAlias(was);
			ModeKeepMenuOpen keepMenu;	// disengage replaces the whole Config
			ModeManager_Disengage();
			resetValues();			// same post-change refresh as the chips
			dbglog("[ModeUI] Restore INI -> disengaged %s owned=%d", wasAlias, wasOwned);
			PfNote("mode -> none");
			// Banner + button state for THIS frame follow the new mode.
			cur = ModeManager_GetMode();
			active = (cur != MODE_NONE);
			owned = active ? ModeManager_GetOwnedCount(cur) : 0;
		}
	}

	// --- active-mode banner --------------------------------------------
	// Full-width glass strip: 2px accent bar, MODE + name + owned-field
	// count, then the way back; the exact INI value sits at the right edge.
	// Always one line at 1000px and wider - the tail and the provenance
	// label each drop whole rather than clip.
	const float barH = ImGui::GetTextLineHeight() + 15.0f;
	ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + h + st.ItemSpacing.y));
	ImGui::Dummy(ImVec2(avail, barH));
	const ImVec2 ba = ImGui::GetItemRectMin();
	const ImVec2 bb = ImGui::GetItemRectMax();

	if(active){
		dl->AddRectFilled(ba, bb, DmColU32(g_dmPalette.bgRaised, 0.62f), r);
		dl->AddRect(ba, bb, DmColU32(g_dmPalette.accentDim, 0.55f), r, 1.0f);
		dl->AddRectFilled(ImVec2(ba.x + 1.0f, ba.y + 1.0f),
			ImVec2(ba.x + 3.0f, bb.y - 1.0f), DmColU32(g_dmPalette.accent, 0.95f), 1.0f);
	}else{
		dl->AddRectFilled(ba, bb, DmColU32(g_dmPalette.bgPanel, 0.55f), r);
		dl->AddRect(ba, bb, DmColU32(g_dmPalette.border, 0.85f), r, 1.0f);
		dl->AddRectFilled(ImVec2(ba.x + 1.0f, ba.y + 1.0f),
			ImVec2(ba.x + 3.0f, bb.y - 1.0f), DmColU32(g_dmPalette.accentDim, 0.45f), 1.0f);
	}

	char modeName[24];
	char detailBuf[48];
	const char *label = active ? ModeChipLabel(cur) : NULL;
	if(!label)
		label = (cur == MODE_NONE) ? ModeManager_GetAlias(cur) : ModeUiName(cur);
	ModeUpper(modeName, (int)sizeof(modeName), label);
	if(active){
		if(owned == 1)
			snprintf(detailBuf, sizeof(detailBuf), "1 field owned");
		else
			snprintf(detailBuf, sizeof(detailBuf), "%d fields owned", owned);
	}else{
		snprintf(detailBuf, sizeof(detailBuf), "no mode engaged");
	}

	const ImVec2 tsMode = ImGui::CalcTextSize("MODE");
	const ImVec2 tsName = ImGui::CalcTextSize(modeName);
	const ImVec2 tsDetail = ImGui::CalcTextSize(detailBuf);
	const char *tail;
	if(!active)
		tail = "pick a chip to load a profile - your INI values stay put";
	else if(ModeHasChip(cur))
		tail = "click the lit chip or press Restore INI to get your INI values back";
	else
		tail = "switch it off from the Presets tab";
	const float tailW = ImGui::CalcTextSize(tail).x;
	const float innerRight = bb.x - 14.0f;
	const float ty = ba.y + (barH - ImGui::GetTextLineHeight()) * 0.5f;
	// Drawn 1px rules, not glyph separators: the menu font is ASCII-only.
	const float sepPad = 14.0f;
	float tx = ba.x + 14.0f;

	dl->AddText(ImVec2(tx, ty), DmColU32(g_dmPalette.textDisabled, 1.0f), "MODE");
	tx += tsMode.x + 10.0f;
	dl->AddText(ImVec2(tx, ty),
		DmColU32(active ? g_dmPalette.accent : g_dmPalette.textSecondary, 1.0f), modeName);
	tx += tsName.x + 14.0f;
	dl->AddText(ImVec2(tx, ty),
		DmColU32(active ? g_dmPalette.textPrimary : g_dmPalette.textDisabled, 1.0f), detailBuf);
	tx += tsDetail.x;

	// Recovery hint: contiguous with the state, dropped whole when it cannot
	// fit so the line never clips on a narrow menu.
	if(tx + sepPad * 2.0f + 1.0f + tailW <= innerRight){
		dl->AddLine(ImVec2(tx + sepPad, ba.y + 8.0f),
			ImVec2(tx + sepPad, bb.y - 8.0f),
			DmColU32(g_dmPalette.borderLight, 0.75f), 1.0f);
		dl->AddText(ImVec2(tx + sepPad + 1.0f + sepPad, ty),
			DmColU32(g_dmPalette.textSecondary, 1.0f), tail);
		tx += sepPad * 2.0f + 1.0f + tailW;
	}

	// Provenance at the right edge: the exact value written to skygfx.ini.
	// Drawn only with clearance from the hint, so it never collides.
	char iniBuf[48];
	snprintf(iniBuf, sizeof(iniBuf), "skygfx.ini  active=%s", ModeManager_GetAlias(cur));
	const float iniW = ImGui::CalcTextSize(iniBuf).x;
	if(tx + 40.0f + iniW <= innerRight)
		dl->AddText(ImVec2(innerRight - iniW, ty),
			DmColU32(g_dmPalette.textDisabled, 1.0f), iniBuf);

	// --- reset discoverability: one line, no new popup ------------------
	ImGui::SetCursorScreenPos(ImVec2(start.x + 14.0f, bb.y + st.ItemSpacing.y));
	ImGui::TextDisabled("%s", active
		? "right-click any row to reset it to the mode default"
		: "right-click any row to reset it to its built-in default");
}

// Debug correlation overlay (defined in the overlay section at EOF):
// appends the "F <id> | C 0x<code>" readout to the CURRENT ImGui frame's
// foreground draw list. While the debug menu is open, DrawUnifiedDebugMenu
// calls this right before its own Render, so the overlay does NOT need to
// run a second ImGui frame that frame (a second frame per frame drains the
// shared input-event queue and consumes the menu's mouse-down edge - that
// is what killed menu clicks; see DebugOverlayDraw).
static void DebugOverlayAppendTextIfEnabled(void);

void DrawUnifiedDebugMenu(IDirect3DDevice9 *device)
{
	if(!device) return;
	EnsureImGuiInit(device);
	if(!unifiedImGuiInited) return;

	ImGui_ImplWin32_NewFrame();
	ImGui_ImplDX9_NewFrame();
	ImGui::NewFrame();

	if(config->debugMenuOpen){
		bool open = true;

		// Pipe-family snapshot for the raw pipe rows' ivMode sync: taken
		// BEFORE any widget in this frame writes config, so a row's onEdit
		// can tell whether its own edit crossed the IV boundary
		// (PipeRowEdit -> SyncIvModeFromPipeFamily).
		s_pipeFamilyWasIV = PipeFamilyIsIV();
		// Editor-style window: master-detail (section list left, settings
		// pane right). Feed last frame's size back so the window
		// can be enlarged freely but never shrunk below a usable minimum —
		// SetNextWindowSizeConstraints only clamps interactive resizes.
		static ImVec2 s_lastSize(0.0f, 0.0f);
		// Scaled up (menu-overhaul pass): the master-detail layout earns the
		// room - wider nav, wider row columns, more visible rows per pane.
		ImVec2 want = (s_lastSize.x > 0.0f) ? s_lastSize : ImVec2(1480.0f, 880.0f);
		if(want.x < 1120.0f) want.x = 1120.0f;
		if(want.y < 680.0f) want.y = 680.0f;
		ImGui::SetNextWindowSize(want, ImGuiCond_Always);
		if(ImGui::Begin("SkyGFX", &open)){
			s_lastSize = ImGui::GetWindowSize();

			// Aero glass banner. The native title bar stays (drag, collapse
			// and close keep working); this band carries the menu identity
			// and sits above the one search box that filters every tab.
			DrawGlassHeader("SKYGFX  //  RENDER CONTROL", ImVec2(0.0f, 34.0f));
			DrawAccentSeparator();

			// Menu-side range guards (SSAO power, env/chrome, tonemap
			// exposure window) — see ClampMenuRanges().
			ClampMenuRanges();

			// Global settings search. Every tab passes this filter into
			// DrawRowsForTab, so non-matching rows disappear instead of
			// leaving stale values on screen.
			ImGui::PushItemWidth(360.0f);
			ImGui::InputTextWithHint("##settingfilter",
				"search settings (key, label, group)...", s_settingFilter, sizeof(s_settingFilter));
			ImGui::PopItemWidth();
			ImGui::SameLine();
			if(ImGui::Button("Clear filter"))
				s_settingFilter[0] = '\0';
			ImGui::SameLine();
			ImGui::TextDisabled("Filters every section.");

			// Always-visible mode selector: eight chips + one state line,
			// OUTSIDE the master-detail child so it stays pinned above it.
			DrawModeChipRow();

			// === Master-detail area ====================================
			// LEFT: the section list (category-grouped Selectables with
			// ON/OFF pills). RIGHT: the active section's pane. The old
			// two-level tab hierarchy (nine top-level tabs + the fourteen
			// -tab FittingPolicyScroll strip inside Main) is folded into
			// this pair - no tab strip side-scrolls any more, and every
			// section is one click / two arrow presses away.
			//
			// The area lives in a fixed-height child so the Save/Reload row
			// below is always on screen no matter which section is open;
			// scrolling happens inside the nav and detail children.
			// Space for the three action rows below (Save/Reload/Reset +
			// config name + saved-configs combo) plus the window padding.
			static const float kBottomReserve = 116.0f;
			float tabH = ImGui::GetContentRegionAvail().y - kBottomReserve;
			if(tabH < 160.0f)
				tabH = 160.0f;
			ImGui::BeginChild("TabArea", ImVec2(0.0f, tabH), 0,
				ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

			// Clamp the selection against the registry (a stale session
			// index can never index past the table).
			if(s_activeSection < 0 || s_activeSection >= kSectionCount)
				s_activeSection = 0;

			DrawSectionNav();
			ImGui::SameLine();
			ImGui::BeginChild("DetailPane", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
			{
				const MenuSection &s = s_menuSections[s_activeSection];
				if(s.drawFn)
					s.drawFn();					// custom pane (own glass header)
				else
					DrawSectionPane(s);			// generic rows pane
			}
			ImGui::EndChild();	// DetailPane
			ImGui::EndChild();	// TabArea

			// Pipe-conjunction #4: re-derive every frame the menu is open,
			// AFTER all widgets above have written config — covers the
			// Main pane's building/vehicle/colour-filter combos, the Style
			// combo's ApplyPreset, and the IV-mode row's ModeManager toggle
			// (ivModeRowEdit -> profileGTAIV sets pipeline itself, so the
			// derive is an identity there) in one call. (ColourFilter_switch's every-frame derive also
			// covers these next frame; this makes the update same-frame for
			// save/display and matches the spec'd call site.)
			derivePipelineFromPipes(config);

			// === Actions (outside tabs) ===
			DrawAccentSeparator();
			if(ImGui::Button("Save INI")){
				PfNote("save INI (cfg %d)", currentConfig);
				saveConfig();
				char savePath[MAX_PATH];
				snprintf(savePath, MAX_PATH, "%sskygfx.ini", asipath);
				MenuWriteMissingIniKeys(savePath);
			}
			ImGui::SameLine();
			if(ImGui::Button("Reload INI")){
				PfNote("reload INI");
				reloadAllInis();
			}
			ImGui::SameLine();
			if(ImGui::Button("Reset Values")){
				PfNote("reset values");
				resetValues();
			}
			ImGui::SameLine();
			if(ImGui::Button("Copy settings as INI"))
				CopySettingsAsIni();
			ImGui::SameLine();
			if(ImGui::GetTime() - s_copyStatusTime < 6.0 && s_copyStatus[0])
				ImGui::TextDisabled("%s", s_copyStatus);
			else
				ImGui::Text("Config: %d / %d", currentConfig, numConfigs);

			// --- Named configs: save current settings as configs/<name>.ini ---
			ImGui::PushItemWidth(170);
			ImGui::InputText("Config name##cfg", s_cfgNameBuf, sizeof(s_cfgNameBuf));
			ImGui::PopItemWidth();
			ImGui::SameLine();
			if(ImGui::Button("Save as New Config")){
				if(!IsValidCfgName(s_cfgNameBuf)){
					if(dbglog_throttle("cfg_saveas"))
						dbglog("[Menu] Save as New Config: rejected invalid name '%s'", s_cfgNameBuf);
				}else{
					char dir[MAX_PATH], path[MAX_PATH];
					snprintf(dir, MAX_PATH, "%sconfigs", asipath);
					CreateDirectoryA(dir, NULL);	// fine if it already exists
					snprintf(path, MAX_PATH, "%sconfigs\\%s.ini", asipath, s_cfgNameBuf);
					saveConfigTo(path);	// logs path + ok/fail itself (throttled)
					MenuWriteMissingIniKeys(path);
					RefreshCfgList();
				}
			}

			// --- Selector: pick a previously saved config and load it ---
			int cfgIdx = -1;
			for(int i = 0; i < (int)s_cfgList.size(); i++)
				if(s_cfgList[i] == s_cfgSel){ cfgIdx = i; break; }
			if(ImGui::BeginCombo("Saved configs##cfg", s_cfgSel.empty() ? "(select...)" : s_cfgSel.c_str())){
				RefreshCfgList();	// rescan on open — picks up files added while the game runs
				cfgIdx = -1;
				for(int i = 0; i < (int)s_cfgList.size(); i++){
					if(s_cfgList[i] == s_cfgSel)
						cfgIdx = i;
					if(ImGui::Selectable(s_cfgList[i].c_str(), i == cfgIdx))
						s_cfgSel = s_cfgList[i];
				}
				if(s_cfgList.empty())
					ImGui::TextDisabled("(no saved configs - use \"Save as New Config\")");
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if(ImGui::Button("Load Config") && !s_cfgSel.empty()){
				char path[MAX_PATH];
				snprintf(path, MAX_PATH, "%sconfigs\\%s.ini", asipath, s_cfgSel.c_str());
				loadConfigFile(path);	// logs path + ok/fail itself (throttled)
			}
		}
		ImGui::End();
		if(!open) config->debugMenuOpen = 0;
	}

	// Correlation readout rides on THIS frame's foreground draw list while
	// the menu is open. DebugOverlayDraw suppresses its standalone frame in
	// that state, so the readout stays visible without a second ImGui frame
	// competing for the shared input-event queue. (Rendered at the menu's
	// draw point rather than screen-final - acceptable while the menu, which
	// is the thing being photographed alongside it, is itself drawn here.)
	DebugOverlayAppendTextIfEnabled();
	ImGui::Render();
	ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
	OvLogFontAtlas("menu");
}

// ============================================================
// ==== Debug correlation overlay (diagnostic readout) =========
// ============================================================
// One line, top-left: "F <frameId> | C 0x<code>". The SAME numbers are
// stamped by the debug-handl lane on EVERY skygfx_dbg.log line as
// [#<frameId>:<code>], so a photo of the screen can be matched to the
// log lines of that exact frame.
//
// Draw point: DebugOverlayDraw() runs at the very end of the
// DrawFinalEffects_pf thunk (main.cpp, the 0x53EBE9 hook) - after the
// whole PostFX chain (colour filter, tonemap, SMAA, YCbCr) - so the
// readout is never colour-filtered, tonemapped or blurred. With the debug
// menu CLOSED it runs its OWN ImGui frame (foreground draw-list text
// only, no window/panel) - the only ImGui frame of that frame. With the
// menu OPEN it must NOT run a second ImGui frame: both frames share one
// context and one input-event queue, and the second frame's NewFrame()
// drains that queue and consumes the menu's mouse-down edge
// (io.MouseClicked is a per-frame derivative; an edge spent in this
// windowless frame also latches io.MouseDownOwned=false, which then
// clears HoveredWindow in every menu frame while the button is held) -
// that is exactly how menu clicks died. While the menu is open the
// readout is instead appended to the menu frame's foreground draw list
// (DebugOverlayAppendTextIfEnabled, called from DrawUnifiedDebugMenu
// before its Render), so it stays visible either way.
//
// Toggle:  INI  [SkyGfx] debugOverlay = 0/1  (default 0 = OFF — the full
//          ImGui frame every game frame measured 3-7% CPU; F9 still toggles
//          at runtime, and the menu's Save INI writer persists the flag)
//          hotkey F9 (edge-triggered)
// F9 audited free in this repo: F5 = config->keys[0], F8 = main.cpp,
// F10 = postfx dump arming, F4/F7 commented out, Ctrl+4 = debug menu.
//
// TODO(debug-handl lane): DebugFrameId()/DebugFrameCode() move into
// src/core/debug_handl.h when that lane lands. The local decls below are
// a deliberate stand-in (identical redeclaration stays legal C++).
unsigned DebugFrameId(void);
unsigned DebugFrameCode(void);

#define DEBUG_OVERLAY_HOTKEY    VK_F9

static bool s_ovInited      = false;
// (s_ovEnabled is defined at the top of this file so BuildSettingRows can
//  bind the Diagnostics row to it; s_ovKeyPrevDown stays overlay-local.)
static bool s_ovKeyPrevDown = false;

// Runtime toggle (also callable from a main.cpp key-routing hunk if the
// orchestrator prefers polling there instead of inside the draw call).
void
DebugOverlayToggle(void)
{
	s_ovEnabled = !s_ovEnabled;
	dbglog("[ovl] correlation overlay toggled -> %d", s_ovEnabled);
}

static void
DebugOverlayInit(void)
{
	if(s_ovInited) return;
	s_ovInited = true;
	// Same mechanism as the other debug flags: read the active
	// skygfx*.ini once, at overlay init. Default 0 = OFF (PERF R2: the
	// always-on ImGui frame cost 3-7% CPU per game frame).
	char path[MAX_PATH];
	int v = 0;
	if(CurrentIniPath(path, sizeof(path)))
		v = (int)GetPrivateProfileIntA("SkyGfx", "debugOverlay", 0, path);
	s_ovEnabled = (v != 0);
	dbglog("[ovl] correlation overlay init: enabled=%d (F9 toggles; INI debugOverlay)", s_ovEnabled);
}

// Append the "F <id> | C 0x<code>" readout to the CURRENT ImGui frame's
// foreground draw list (top-left, 1px outline - same style as the standalone
// overlay below). Called from DrawUnifiedDebugMenu right before its Render
// while the debug menu is open; must only run between NewFrame and Render of
// an existing frame. No frame machinery here - no EnsureImGuiInit, no
// NewFrame, no Render (the caller's frame owns all of that).
static void
DebugOverlayAppendTextIfEnabled(void)
{
	DebugOverlayInit();
	if(!s_ovEnabled) return;

	const unsigned fid  = DebugFrameId();
	const unsigned code = DebugFrameCode();
	char txt[64];
	snprintf(txt, sizeof(txt), "F %u | C 0x%04X", fid, code);

	// Small monospace, top-left. The default ImGui font (ProggyClean) is a
	// monospace bitmap face; 14 px keeps the readout tiny but photo-legible.
	ImDrawList *dl = ImGui::GetForegroundDrawList();
	ImFont *font = ImGui::GetFont();
	ImFontAtlas *atlas = ImGui::GetIO().Fonts;
	// Never submit glyphs without a live font + atlas texture. A null font,
	// an empty atlas or a texture that has not been recreated after a device
	// Reset makes AddText() sample garbage (boxes/blank) or assert.
	if(dl == NULL || font == NULL || atlas == NULL ||
	   atlas->Fonts.Size == 0 || atlas->TexData == NULL)
		return;
	const float fs = 14.0f;
	const ImVec2 pos(12.0f, 10.0f);
	// 1 px full outline (8 offsets), near-black: contrast on bright sky
	// comes from the outline, on dark night from the light core. No panel,
	// no box - this is a diagnostic instrument, not a menu.
	const ImU32 shadow = IM_COL32(0, 0, 0, 235);
	for(int dy = -1; dy <= 1; dy++)
		for(int dx = -1; dx <= 1; dx++)
			if(dx != 0 || dy != 0)
				dl->AddText(font, fs, ImVec2(pos.x + (float)dx, pos.y + (float)dy), shadow, txt);
	dl->AddText(font, fs, pos, IM_COL32(238, 244, 232, 255), txt);
}

void
DebugOverlayDraw(IDirect3DDevice9 *device)
{
	// Hotkey first, so the overlay can be brought back while hidden. Polled
	// every frame, menu open or closed: GetAsyncKeyState is a passive status
	// query - it consumes nothing and cannot swallow menu key events, and F9
	// is audited free of menu/game keys (see the F9 audit note above).
	{
		const bool down = (GetAsyncKeyState(DEBUG_OVERLAY_HOTKEY) & 0x8000) != 0;
		if(down && !s_ovKeyPrevDown)
			DebugOverlayToggle();
		s_ovKeyPrevDown = down;
	}
	if(!device) return;
	DebugOverlayInit();
	if(!s_ovEnabled) return;
	// Menu open -> the menu's own ImGui frame already carried the readout
	// (DebugOverlayAppendTextIfEnabled from DrawUnifiedDebugMenu). Do NOT
	// run a second ImGui frame here: it would drain the shared input-event
	// queue and consume the menu's mouse-down edge, killing menu clicks
	// (full mechanism in the section header comment above). With the menu
	// closed this standalone frame is the only ImGui frame of the frame, so
	// it is safe - and running it is the overlay's whole purpose.
	if(config && config->debugMenuOpen) return;
	EnsureImGuiInit(device);
	if(!unifiedImGuiInited) return;

	const unsigned fid  = DebugFrameId();
	const unsigned code = DebugFrameCode();
	char txt[64];
	snprintf(txt, sizeof(txt), "F %u | C 0x%04X", fid, code);

	// Standalone ImGui frame - foreground draw-list text only. This frame
	// must leave cursor behaviour untouched: GTA SA hides the OS cursor
	// and the menu draws its own software cursor only while it is open.
	ImGuiIO &io = ImGui::GetIO();
	const bool prevDrawCursor = io.MouseDrawCursor;
	const ImGuiConfigFlags prevFlags = io.ConfigFlags;
	io.MouseDrawCursor = false;
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
	ImGui_ImplWin32_NewFrame();
	ImGui_ImplDX9_NewFrame();
	// Back-to-back frames (menu + overlay) can see a 0-tick QPC delta;
	// ImGui::NewFrame asserts on DeltaTime <= 0. Still reachable on the
	// frame where the menu closes mid-frame (menu frame ran, then this one).
	if(io.DeltaTime <= 0.0f) io.DeltaTime = 1.0f / 60.0f;
	ImGui::NewFrame();

	// Small monospace, top-left. The default ImGui font (ProggyClean) is a
	// monospace bitmap face; 14 px keeps the readout tiny but photo-legible.
	// Same atlas guard as the menu-open path: with no built font / no atlas
	// texture the readout would be boxes or blank - skip it, but still close
	// the frame below (NewFrame without Render trips ImGui's frame assert).
	ImDrawList *dl = ImGui::GetForegroundDrawList();
	ImFont *font = ImGui::GetFont();
	ImFontAtlas *atlas = ImGui::GetIO().Fonts;
	const bool atlasOk = (dl != NULL && font != NULL && atlas != NULL &&
	                      atlas->Fonts.Size > 0 && atlas->TexData != NULL);
	const float fs = 14.0f;
	const ImVec2 pos(12.0f, 10.0f);
	// 1 px full outline (8 offsets), near-black: contrast on bright sky
	// comes from the outline, on dark night from the light core. No panel,
	// no box - this is a diagnostic instrument, not a menu.
	const ImU32 shadow = IM_COL32(0, 0, 0, 235);
	if(atlasOk){
		for(int dy = -1; dy <= 1; dy++)
			for(int dx = -1; dx <= 1; dx++)
				if(dx != 0 || dy != 0)
					dl->AddText(font, fs, ImVec2(pos.x + (float)dx, pos.y + (float)dy), shadow, txt);
		dl->AddText(font, fs, pos, IM_COL32(238, 244, 232, 255), txt);
	}

	ImGui::Render();
	ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
	OvLogFontAtlas("overlay");
	io.MouseDrawCursor = prevDrawCursor;
	io.ConfigFlags = prevFlags;
}
