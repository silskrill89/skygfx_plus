#pragma once

// modeManager.h — Graphics mode manager.
//
// Exactly ONE mode is active at a time (or none). A mode is a named
// rendering profile layered onto the live Config, FIELD BY FIELD:
//
//   enter  (NONE -> A) : snapshot the live Config into the per-config-slot
//                        base (s_baseConfig[slot]) BEFORE the profile runs,
//                        then write ONLY profile(A)'s owned fields.
//   switch (A -> B)    : revert A's owned fields to the SAME slot base,
//                        then write B's owned fields — profiles never stack
//                        on each other.
//   toggle off (A -> -): revert A's owned fields to the slot base, current
//                        becomes MODE_NONE.
//
// Non-owned fields are never written by an engage or a disengage, so settings
// the user changes while a mode is on (and any non-mode feature) survive the
// whole run. The base is the post-INI state: [Modes] active= is applied at
// the very END of readIniFile(), after every other key (incl. qualityPreset).
//
// The canonical tables (ModeProfile / g_modeProfiles) live in
// modeManager.cpp. ApplyMode() is the ONE writer of mode-owned fields;
// ModeManager_SetMode() is kept as a thin alias for existing callers.
// Registered listeners are notified at the end of ApplyMode (after
// resetValues()) so downstream renderer state can react.

#include "skygfx.h"	// Config — the per-slot base and every profile operate on it

enum ModeId
{
	// The six user-facing modes (3 vintage + 3 modern) plus None/Custom.
	MODE_NONE = 0,		// no profile: live Config is the user's (legacy behaviour)
	MODE_PS2,		// vintage: PS2
	MODE_XBOX,		// vintage: Xbox
	MODE_PC_VINTAGE,	// vintage: original PC (Xbox building pipe + CAR_PC)
	MODE_GTAIV,		// modern: GTA IV
	MODE_GTAV,		// modern: GTA V (probe-gated, water only)
	MODE_MODERN,		// modern: PBR / "ULTRA" profile
	MODE_CUSTOM,		// user-defined: no profile fields, per-slot base only
	MODE_MOBILE,		// LEGACY alias, hidden from the 6+Custom picker

	MODE_COUNT
};

// Max ModeManager_RegisterListener() slots.
#define MODEMANAGER_MAX_LISTENERS 16

// Reset manager state to MODE_NONE (clears the per-slot base flags) and log
// the "[mode] manager init" line. Safe to call again on reload — registered
// listeners are kept.
void ModeManager_Init(void);

// The ONE apply path. Writes ONLY the target profile's owned Config fields
// (a NONE/CUSTOM target owns none, so it writes nothing); the per-config-slot
// pre-mode base supplies the disengage values. Calls resetValues(), emits the
// "[Mode] engage/disengage ..." + legacy "[Mode] apply ..." receipts and
// notifies listeners with (oldMode, newMode). Passing the CURRENT id is the
// toggle-OFF path (same restore as ModeManager_Disengage()); passing
// MODE_NONE while a mode is active does the same restore. Out-of-range ids
// and a missing config are ignored (never throws).
void ApplyMode(ModeId id);

// Kept for existing callers (debug menu / IV rows). Thin alias of ApplyMode().
// The UI toggle path is explicit here: passing the CURRENT id calls
// ModeManager_Disengage() so clicking the lit chip always restores the base.
void ModeManager_SetMode(ModeId id);

// EXPLICIT toggle-off entry point (UI chip clicked while lit). Reverts the
// active mode's OWNED fields to the per-slot base, latches MODE_NONE, calls
// resetValues() and emits "[Mode] disengage restored=<alias> owned=%d ...".
// No-op with a throttled receipt when no mode is active. All existing
// signatures are unchanged; this is additive.
void ModeManager_Disengage(void);

// INI RELOAD hook: call ONCE at the END of readIniFile(), after the
// [Modes] active= ApplyMode() line (main.cpp owns that call site).
// Re-snapshots the live Config — which the reload just refilled with the
// user's INI values — as the new per-slot base, then re-layers the active
// profile's owned fields. No-op when the [Modes] line on this pass already
// applied the active profile (its owned fields match, so the base is fresh).
// Without this, a reload that SKIPS the same-id ApplyMode leaves the
// pre-reload base and a later disengage restores stale values.
void ModeManager_RefreshBase(void);

// Currently active mode — MODE_NONE while no mode is on.
ModeId ModeManager_GetMode(void);

// ---- Mode introspection (read-only, no allocation) ----------------------
// The debug menu renders a truthful per-row "MODE" badge with these instead
// of duplicating the profile table: a row is mode-owned iff the ACTIVE mode's
// profile sets its INI key. Keys are the exact skygfx.ini key names and
// compare case-insensitively (StrAssoc::get semantics). MODE_NONE and
// MODE_CUSTOM own nothing.

// Returns true when the ACTIVE mode owns configKey, and writes that mode's
// profile value into *out (as float, for int/bool/float alike).
// *out may be NULL when only ownership matters; it is untouched on false.
bool ModeManager_GetOwnedValue(const char *configKey, float *out);

// Returns the iniAlias of a mode id ("None","PS2","Xbox","PC","GTAIV","GTAV","Modern","Custom","Mobile").
const char *ModeManager_GetAlias(ModeId id);

// Convenience: is this key owned by the active mode?
bool ModeManager_IsOwnedKey(const char *configKey);

// Number of Config fields the mode's profile owns (0 for NONE/CUSTOM and
// invalid ids). Useful for the UI summary line.
int ModeManager_GetOwnedCount(ModeId id);

// Drop the active mode latch WITHOUT restoring the per-slot base and WITHOUT
// touching Config. For direct Config writers that bypass ApplyMode (legacy
// debug-menu bool row, menu_inject toggle, ApplyPreset): the caller has
// already written the Config state it wants, so the latch must go or a later
// same-id toggle-off would restore the pre-mode base over it. The base is
// kept: it is still the user's pre-mode (INI) state, and ApplyMode() does NOT
// re-snapshot over it while s_haveBase[slot] is set, so a later engage and
// disengage still return to the INI values (ModeManager_RefreshBase() is the
// reload path that deliberately replaces it).
void ModeManager_ClearLatch(void);

// fn is called after every real mode change with (oldMode, newMode), after
// resetValues(). Duplicate registrations are ignored; the table holds
// MODEMANAGER_MAX_LISTENERS entries.
void ModeManager_RegisterListener(void (*fn)(ModeId oldMode, ModeId newMode));
