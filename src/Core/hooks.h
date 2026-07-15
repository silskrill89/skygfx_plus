#ifndef HOOKS_H
#define HOOKS_H

// hooks.h — Game hook installation.
// InstallAllHooks: all patches/Nops/InjectHooks from main DllMain path.
// InjectDelayedPatches: patches that must run during CGame::Initialise.
//
// To strip all hooks for a release: define HOOKS_DISABLED.

#include <windows.h>

#ifdef HOOKS_DISABLED

#define hooks_installAll()    ((void)0)
#define hooks_installDelayed() ((void)0)

#else

// Install all game hooks. Called from DllMain after packer check.
void InstallAllHooks(void);

// Install delayed patches (called from IsAlreadyRunning hook during CGame::Initialise).
int  InjectDelayedPatches(void);

#endif // HOOKS_DISABLED

#endif // HOOKS_H
