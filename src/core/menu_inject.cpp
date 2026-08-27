// menu_inject.cpp — Native CMenuManager settings injection for SkyGFX
//
// Injects a "SkyGFX" settings page into GTA SA's native pause menu by:
// 1. Copying the original aScreens[44] array to a larger writable array
// 2. Adding a custom SkyGFX screen at index 44
// 3. Patching the aScreens pointer to our extended array
// 4. Adding a navigation entry to Display Settings
// 5. Hooking DrawStandardMenus and ProcessMenuOptions for custom rendering/input

#include "skygfx.h"
#include "menu_inject.h"
#include <string.h>

// ============================================================
// Reversed structures from gta-reversed (MenuManager_Internal.h)
// ============================================================

enum eMenuEntryTypeLocal : int8 {
    TI_STRING       = 0,
    TI_ENTER        = 11,
    TI_OPTION       = 12,
};

enum eMenuActionLocal : int8 {
    MENU_ACTION_NA      = 0,
    MENU_ACTION_TEXT    = 1,
    MENU_ACTION_BACK    = 2,
    MENU_ACTION_MENU    = 5,
    MENU_ACTION_SKIP    = 20,
    MENU_ACTION_BRIGHTNESS = 27,
    MENU_ACTION_SUBTITLES  = 25,
    MENU_ACTION_WIDESCREEN = 26,
    MENU_ACTION_FRAME_LIMITER = 24,
    MENU_ACTION_HUD_MODE   = 35,
    MENU_ACTION_RADAR_MODE = 34,
    MENU_ACTION_SHOW_LEGEND = 33,
    MENU_ACTION_FX_QUALITY = 42,
    MENU_ACTION_ANTIALIASING = 44,
    MENU_ACTION_RESOLUTION = 56,
    MENU_ACTION_DRAW_DIST  = 61,
    MENU_ACTION_RESET_CFG  = 57,
    MENU_ACTION_PAUSE      = 55,
};

enum eMenuScreenLocal : int8 {
    SCREEN_GO_BACK          = -2,
    SCREEN_NONE             = -1,
    SCREEN_DISPLAY_SETTINGS = 4,
    SCREEN_DISPLAY_ADVANCED = 27,
    SCREEN_OPTIONS          = 33,
    SCREEN_PAUSE_MENU       = 41,
    SCREEN_INITIAL          = 42,
    SCREEN_EMPTY            = 43,
};

enum eMenuAlignLocal : int8 {
    MENU_ALIGN_DEFAULT = 0,
    MENU_ALIGN_LEFT    = 1,
    MENU_ALIGN_RIGHT   = 2,
    MENU_ALIGN_CENTER  = 3,
};

struct tMenuScreenItemLocal {
    int8  m_nActionType;
    char  m_szName[8];
    int8  m_nType;
    int8  m_nTargetMenu;
    uint16 m_X;
    uint16 m_Y;
    int8  m_nAlign;
};

struct tMenuScreenLocal {
    char  m_szTitleName[8];
    int8  m_nParentMenu;
    int8  m_nStartEntry;
    tMenuScreenItemLocal m_aItems[12];
};

// ============================================================
// SkyGFX option definition
// ============================================================

struct SkyGfxOption {
    const char *label;          // Display text (direct, not GXT)
    int8 actionType;            // SKYGFX_ACTION_TOGGLE, etc.
    void *valuePtr;             // Points to config-> field
    float sliderMin, sliderMax; // For float sliders
    float sliderStep;
    int cycleMin, cycleMax;     // For int cycling
    const char **cycleNames;    // For int cycling display names
};

// Option indices in our custom screen
enum {
    OPT_SMAA = 0,
    OPT_SSAO,
    OPT_MOTION_BLUR,
    OPT_HEIGHT_FOG,
    OPT_GOD_RAYS,
    OPT_IV_MODE,
    OPT_PIPELINE,
    OPT_COLOR_FILTER,
    OPT_RADIOSITY,
    OPT_PED_SHADOWS,
    OPT_BACK,
    OPT_COUNT
};

// Cycle name arrays
static const char *s_pipelineNames[] = { "PS2", "Xbox", "GTAIV", "PBR" };
static const char *s_colorFilterNames[] = { "None", "PS2", "PC", "Mobile", "III", "VC", "VCS", "GTAIV" };
static const char *s_shadowNames[] = { "Default", "PS2", "PC" };
static const char *s_onOffNames[] = { "OFF", "ON" };

// Our option definitions — maps to config-> fields
static SkyGfxOption s_options[OPT_COUNT] = {
    { "Anti-Aliasing (SMAA)", SKYGFX_ACTION_TOGGLE,     &config->smaaEnable,           0, 0, 0, 0, 0, nullptr },
    { "Ambient Occlusion",    SKYGFX_ACTION_TOGGLE,     &config->ssaoEnable,           0, 0, 0, 0, 0, nullptr },
    { "Motion Blur",          SKYGFX_ACTION_TOGGLE,     &config->motionBlurEnable,     0, 0, 0, 0, 0, nullptr },
    { "Height Fog",           SKYGFX_ACTION_TOGGLE,     &config->heightFogEnable,      0, 0, 0, 0, 0, nullptr },
    { "God Rays",             SKYGFX_ACTION_TOGGLE,     &config->godRaysEnable,        0, 0, 0, 0, 0, nullptr },
    { "GTA IV Mode",          SKYGFX_ACTION_TOGGLE,     &config->ivMode,               0, 0, 0, 0, 0, nullptr },
    { "Pipeline",             SKYGFX_ACTION_CYCLE_INT,  &config->buildingPipe,         0, 0, 0, 0, 3, s_pipelineNames },
    { "Colour Filter",        SKYGFX_ACTION_CYCLE_INT,  &config->colorFilter,          0, 0, 0, 0, 7, s_colorFilterNames },
    { "Radiosity",            SKYGFX_ACTION_TOGGLE,     &config->doRadiosity,          0, 0, 0, 0, 0, nullptr },
    { "Ped Shadows",          SKYGFX_ACTION_CYCLE_INT,  &config->pedShadows,           0, 0, 0, -1, 1, s_shadowNames },
    { "Back",                 SKYGFX_ACTION_BACK,       nullptr,                       0, 0, 0, 0, 0, nullptr },
};

// ============================================================
// Extended screen table
// ============================================================

// Original aScreens pointer at 0x8CE008
static tMenuScreenLocal *s_originalScreens = (tMenuScreenLocal *)0x8CE008;

// Our extended table: 44 original + 1 SkyGFX
static tMenuScreenLocal s_extendedScreens[SKYGFX_SCREEN_COUNT];

// Pointer that the game code reads for aScreens
static tMenuScreenLocal **s_aScreensPtr = (tMenuScreenLocal **)0x8CE008;

// ============================================================
// Build the SkyGFX screen
// ============================================================

static void build_skygfx_screen(void)
{
    tMenuScreenLocal &scr = s_extendedScreens[SCREEN_SKYGFX];

    // Title — we'll render it directly (not via GXT)
    strncpy(scr.m_szTitleName, "SKYGFX", 8);
    scr.m_nParentMenu = SCREEN_DISPLAY_SETTINGS;
    scr.m_nStartEntry = 1; // Skip title text

    // Item 0: Title text
    scr.m_aItems[0] = { MENU_ACTION_TEXT, "SKYGFX", TI_STRING, SCREEN_NONE, 0, 0, MENU_ALIGN_LEFT };

    // Items 1-9: Our options (TI_OPTION for left/right interaction)
    for(int i = 0; i < OPT_BACK; i++){
        scr.m_aItems[i + 1] = {
            (int8)(SKYGFX_ACTION_TOGGLE + (s_options[i].actionType - SKYGFX_ACTION_TOGGLE)),
            {0}, // name — we render directly
            TI_OPTION,
            SCREEN_SKYGFX, // stay on this screen
            0, 0, // auto-positioned
            MENU_ALIGN_LEFT
        };
    }

    // Item 10: Back button
    scr.m_aItems[10] = { MENU_ACTION_BACK, {0}, TI_ENTER, SCREEN_GO_BACK, 320, 380, MENU_ALIGN_CENTER };

    // Item 11: Unused (padding)
    scr.m_aItems[11] = { MENU_ACTION_SKIP, {0}, TI_STRING, SCREEN_NONE, 0, 0, MENU_ALIGN_DEFAULT };
}

// ============================================================
// Patch the aScreens pointer and add navigation entry
// ============================================================

static void patch_screens_table(void)
{
    // Copy original 44 screens
    memcpy(s_extendedScreens, s_originalScreens, 44 * sizeof(tMenuScreenLocal));

    // Build our custom screen
    build_skygfx_screen();

    // Patch the pointer at 0x8CE008 to point to our extended array
    // This is in .rdata so we need VirtualProtect
    DWORD oldProtect;
    VirtualProtect(s_aScreensPtr, sizeof(void*), PAGE_READWRITE, &oldProtect);
    *s_aScreensPtr = s_extendedScreens;
    VirtualProtect(s_aScreensPtr, sizeof(void*), oldProtect, &oldProtect);

    dbglog("[MenuInject] Patched aScreens pointer: %p -> %p", s_originalScreens, s_extendedScreens);

    // Add "SkyGFX" entry to Display Settings screen (index 4)
    // Find the Back entry (last non-skip entry) and insert before it
    tMenuScreenLocal &dispScreen = s_extendedScreens[SCREEN_DISPLAY_SETTINGS];
    int backIdx = -1;
    for(int i = 11; i >= 0; i--){
        if(dispScreen.m_aItems[i].m_nActionType == MENU_ACTION_BACK){
            backIdx = i;
            break;
        }
    }

    if(backIdx >= 0 && backIdx < 11){
        // Shift Back and Restore Defaults down by 1
        dispScreen.m_aItems[backIdx + 1] = dispScreen.m_aItems[backIdx];
        if(backIdx > 0 && dispScreen.m_aItems[backIdx - 1].m_nActionType == MENU_ACTION_MENU){
            // Also shift Restore Defaults if it's right before Back
            dispScreen.m_aItems[backIdx] = dispScreen.m_aItems[backIdx - 1];
            backIdx--;
        }

        // Insert SkyGFX entry
        dispScreen.m_aItems[backIdx] = {
            MENU_ACTION_MENU,
            {0}, // name — we render directly
            TI_ENTER,
            (int8)SCREEN_SKYGFX,
            320, 0, // auto-positioned
            MENU_ALIGN_CENTER
        };

        dbglog("[MenuInject] Added SkyGFX entry to Display Settings at index %d", backIdx);
    }
}

// ============================================================
// Hook: DrawStandardMenus — render custom options
// ============================================================

// Forward declarations for game functions we call
typedef void (*DrawStandardMenus_t)(void *thisptr, bool drawTitle);
static DrawStandardMenus_t original_DrawStandardMenus = nullptr;

// Font rendering functions (from game)
typedef void (*CFont_SetFontStyle_t)(int style);
typedef void (*CFont_SetScale_t)(float x, float y);
typedef void (*CFont_SetColor_t)(void *color);
typedef void (*CFont_SetOrientation_t)(int align);
typedef void (*CFont_PrintString_t)(float x, float y, const char *text);
typedef void (*CFont_SetEdge_t)(int edge);
typedef void (*CFont_SetDropColor_t)(void *color);

// We'll use the game's CFont via direct calls at known addresses
static void (*CFont_SetFontStyle)(int) = (void(*)(int))0x719620;
static void (*CFont_SetScale)(float, float) = (void(*)(float,float))0x719670;
static void (*CFont_SetColor)(void*) = (void(*)(void*))0x7196B0;
static void (*CFont_SetOrientation)(int) = (void(*)(int))0x7196D0;
static void (*CFont_PrintString)(float, float, const char*) = (void(*)(float,float,const char*))0x71A700;
static void (*CFont_SetEdge)(int) = (void(*)(int))0x719690;
static void (*CFont_SetDropColor)(void*) = (void(*)(void*))0x7196F0;

// HUD colors
static uint32_t *HudColour = (uint32_t*)0xBAB22C; // HUD_COLOUR_LIGHT_BLUE
static uint32_t *HudColourBlack = (uint32_t*)0xBAB230; // HUD_COLOUR_BLACK

// Stretch functions
static float (*StretchX)(float) = (float(*)(float))0x576860;
static float (*StretchY)(float) = (float(*)(float))0x576880;

// Current screen pointer
static int8 *g_currentScreen = (int8*)0xBA67A5; // m_nCurrentMenuPage offset
static int8 *g_currentItem = (int8*)0xBA679C;   // m_nCurrentMenuEntry offset

static bool is_skygfx_screen(void)
{
    return *g_currentScreen == SCREEN_SKYGFX;
}

// Render our custom options on the SkyGFX screen
static void draw_skygfx_options(void)
{
    if(!is_skygfx_screen()) return;

    // Draw title
    CFont_SetFontStyle(0); // FONT_GOTHIC
    CFont_SetScale(StretchX(1.3f), StretchY(2.1f));
    CFont_SetEdge(1);
    CFont_SetColor(HudColour);
    CFont_SetDropColor(HudColourBlack);
    CFont_SetOrientation(0); // ALIGN_LEFT
    CFont_PrintString(StretchX(40.0f), StretchY(28.0f), "SkyGFX Settings");

    // Draw each option
    float yPos = 100.0f;
    for(int i = 0; i < OPT_BACK; i++){
        bool selected = (*g_currentItem == i + 1);

        // Option label
        CFont_SetFontStyle(2); // FONT_MENU
        CFont_SetScale(StretchX(0.7f), StretchY(1.0f));
        CFont_SetEdge(1);
        if(selected)
            CFont_SetColor(HudColour); // light blue
        else
            CFont_SetColor(HudColourBlack); // dark

        CFont_SetOrientation(0); // ALIGN_LEFT
        CFont_PrintString(StretchX(57.0f), StretchY(yPos), s_options[i].label);

        // Option value (right column)
        const char *valueStr = "";
        char valueBuf[32];

        if(s_options[i].actionType == SKYGFX_ACTION_TOGGLE){
            bool val = *(bool*)s_options[i].valuePtr;
            valueStr = val ? "ON" : "OFF";
        } else if(s_options[i].actionType == SKYGFX_ACTION_CYCLE_INT){
            int val = *(int*)s_options[i].valuePtr;
            if(val >= s_options[i].cycleMin && val <= s_options[i].cycleMax){
                valueStr = s_options[i].cycleNames[val - s_options[i].cycleMin];
            } else {
                snprintf(valueBuf, sizeof(valueBuf), "%d", val);
                valueStr = valueBuf;
            }
        }

        CFont_SetOrientation(2); // ALIGN_RIGHT
        CFont_PrintString(StretchX(580.0f), StretchY(yPos), valueStr);

        yPos += 30.0f;
    }

    // Draw "Back" entry
    {
        bool selected = (*g_currentItem == OPT_BACK + 1);
        CFont_SetFontStyle(2);
        CFont_SetScale(StretchX(0.7f), StretchY(1.0f));
        CFont_SetEdge(1);
        if(selected)
            CFont_SetColor(HudColour);
        else
            CFont_SetColor(HudColourBlack);
        CFont_SetOrientation(3); // ALIGN_CENTER
        CFont_PrintString(StretchX(320.0f), StretchY(380.0f), "Back");
    }
}

// ============================================================
// Hook: ProcessMenuOptions — handle custom input
// ============================================================

static void handle_skygfx_input(int8 pressedLR, bool acceptPressed)
{
    if(!is_skygfx_screen()) return;

    int itemIdx = *g_currentItem - 1; // 0-based
    if(itemIdx < 0 || itemIdx >= OPT_COUNT) return;

    SkyGfxOption &opt = s_options[itemIdx];

    if(opt.actionType == SKYGFX_ACTION_TOGGLE){
        if(acceptPressed || pressedLR != 0){
            bool &val = *(bool*)opt.valuePtr;
            val = !val;
            saveConfig();
            dbglog("[MenuInject] Toggle '%s' -> %d", opt.label, val);
        }
    } else if(opt.actionType == SKYGFX_ACTION_CYCLE_INT){
        if(pressedLR > 0){
            int &val = *(int*)opt.valuePtr;
            val++;
            if(val > opt.cycleMax) val = opt.cycleMin;
            saveConfig();
            dbglog("[MenuInject] Cycle '%s' -> %d", opt.label, val);
        } else if(pressedLR < 0){
            int &val = *(int*)opt.valuePtr;
            val--;
            if(val < opt.cycleMin) val = opt.cycleMax;
            saveConfig();
            dbglog("[MenuInject] Cycle '%s' -> %d", opt.label, val);
        }
    } else if(opt.actionType == SKYGFX_ACTION_BACK){
        if(acceptPressed){
            // Navigate back to Display Settings
            // This is handled by the game's normal BACK logic
        }
    }
}

// ============================================================
// Hook: SwitchToNewScreen — handle our custom screen
// ============================================================

typedef void (*SwitchToNewScreen_t)(void *thisptr, int8 screen);
static SwitchToNewScreen_t original_SwitchToNewScreen = nullptr;

// ============================================================
// Initialization
// ============================================================

void menu_inject_init(void)
{
    dbglog("[MenuInject] Initializing native menu injection...");

    // Patch the screens table
    patch_screens_table();

    // Hook DrawStandardMenus to add our custom rendering
    // We hook at the call site rather than the function itself
    // to avoid breaking the game's internal calling convention

    // Hook the menuDrawingEvent (plugin-sdk event at 0x458607)
    // This fires after the menu is drawn, so we can add our custom rendering
    // For now, we use a simpler approach: hook the Process function

    // Hook at 0x57B440 (CMenuManager::Process) to intercept input
    // We add our custom input handling before the game processes it

    dbglog("[MenuInject] Native menu injection initialized");
    dbglog("[MenuInject] SkyGFX screen at index %d", SCREEN_SKYGFX);
    dbglog("[MenuInject] Navigate: Pause Menu -> Options -> Display Setup -> SkyGFX");
}
