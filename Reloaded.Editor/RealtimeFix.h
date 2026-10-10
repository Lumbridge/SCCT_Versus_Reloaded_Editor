#pragma once

extern int  g_ReloadedMaxFPS;
extern bool g_ReloadedMuteSounds;
extern bool g_ReloadedNoDuplicateOffset;
extern bool g_ReloadedMinimizeOnPlay;

// Cloth constraint and damping run per step, so it stiffens with frame rate.
extern bool  g_SoftBodyStepRateCapEnabled;
extern float g_SoftBodyMinStepSeconds;

class RealtimeFix
{
public:
    static void Initialize();

    // Called with every editor command before it runs. Before a MAP SAVE it gives each strip door
    // or patch simulation still without render bounds the step its build should have given it.
    static void BeforeEditorCommand(const char* command);
};
