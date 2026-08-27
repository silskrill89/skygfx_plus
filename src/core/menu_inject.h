// menu_inject.h — Native CMenuManager settings injection for SkyGFX
#pragma once

// Initialize the menu injection system.
// Call from InjectDelayedPatches() after all other hooks are installed.
void menu_inject_init(void);

// Custom action types for SkyGFX menu entries.
// These are above the game's MENU_ACTION_COUNT (66) so they don't conflict.
enum eSkyGfxMenuAction : int8 {
    SKYGFX_ACTION_TOGGLE = 100,     // Boolean toggle (ON/OFF)
    SKYGFX_ACTION_SLIDER_FLOAT,     // Float slider
    SKYGFX_ACTION_CYCLE_INT,        // Integer cycling list
    SKYGFX_ACTION_BACK,             // Back to parent menu
};

// Custom screen index (appended after SCREEN_COUNT=44)
enum {
    SCREEN_SKYGFX = 44,
    SKYGFX_SCREEN_COUNT = 45,
};
