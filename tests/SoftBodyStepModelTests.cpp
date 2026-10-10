#include "../Reloaded.Editor/SoftBodyStepModel.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

static void Check(bool passed, const char* message)
{
    if (!passed) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

static bool Near(float a, float b) { return std::fabs(a - b) < 1e-6f; }

int main()
{
    using namespace SoftBodyStepModel;
    const float minStep = 1.0f / 120.0f, maxStep = 0.05f;

    // Render bounds: the constructor's zeroed sphere has none; a built cloth's always has. A cloth
    // gone NaN is not a new simulation, so it stays under the cap.
    Check(!HasRenderBounds(0.0f) && !HasRenderBounds(-0.0f), "A zero radius counted as bounds");
    Check(HasRenderBounds(330.7f) && HasRenderBounds(0.001f), "A built simulation's radius was not bounds");
    const float notANumber = std::numeric_limits<float>::quiet_NaN();
    Check(HasRenderBounds(notANumber), "A NaN radius counted as a new simulation");
    Check(!Choose(true, HasRenderBounds(notANumber), 0.016f, 0.0f).run, "A NaN cloth bypassed the cap");

    // A new simulation's build ends with Update(0). It runs whatever the bank holds, with the 0 it
    // asked for (the engine then applies its own 0.005 s minimum, as the stock editor does).
    Step s = Choose(true, false, 0.0f, 0.0f);
    Check(s.run && s.dt == 0.0f, "A new simulation's first step was skipped with nothing banked");
    s = Choose(true, false, 0.0f, 0.02f);
    Check(s.run && s.dt == 0.0f, "A new simulation's first step took the banked step instead of its own");
    s = Choose(true, false, 0.016f, 0.0f);
    Check(s.run && Near(s.dt, 0.016f), "A simulation without bounds was capped");

    // Once it has bounds, the #14 cap applies: only banked time steps it.
    s = Choose(true, true, 0.004f, 0.0f);
    Check(!s.run, "A step ran with nothing banked");
    s = Choose(true, true, 0.004f, 0.0125f);
    Check(s.run && Near(s.dt, 0.0125f), "A banked step was not passed on");
    s = Choose(true, true, 0.0f, 0.0f);
    Check(!s.run, "A rebuilt-and-bounded simulation's zero step bypassed the cap");

    // With the cap off every call runs as asked.
    s = Choose(false, true, 0.004f, 0.0f);
    Check(s.run && Near(s.dt, 0.004f), "The uncapped path changed the step");
    s = Choose(false, false, 0.0f, 0.0f);
    Check(s.run && s.dt == 0.0f, "The uncapped path skipped a first step");

    // Banking (#14): time adds up until it reaches the minimum, then goes as one step.
    float pending = 0.0f;
    Check(Bank(pending, 0.003, minStep, maxStep) == 0.0f, "3 ms released a step");
    Check(Bank(pending, 0.003, minStep, maxStep) == 0.0f, "6 ms released a step");
    float step = Bank(pending, 0.003, minStep, maxStep);
    Check(Near(step, 0.009f) && Near(pending, 0.0f), "9 ms banked was not released as one step");
    step = Bank(pending, 2.0, minStep, maxStep);
    Check(Near(step, maxStep) && Near(pending, 0.0f), "A stall was not capped at the engine's maximum step");
    step = Bank(pending, 1.0 / 120.0, minStep, maxStep);
    Check(Near(step, minStep), "A 120 fps frame did not release its step");

    // A body built mid-session in a 240 fps realtime viewport: its build step runs, then it steps
    // at most 120 times a second however often the viewport ticks it.
    pending = 0.0f;
    float banked = 0.0f;
    float radius = 0.0f;
    int updates = 0, ticks = 0;
    s = Choose(true, HasRenderBounds(radius), 0.0f, banked);           // the build's Update(0)
    if (s.run) { ++updates; radius = 324.4f; }                           // its bounds update
    Check(updates == 1 && HasRenderBounds(radius), "The build step did not give the body bounds");
    for (int frame = 0; frame < 240; ++frame)
    {
        banked = Bank(pending, 1.0 / 240.0, minStep, maxStep);
        s = Choose(true, HasRenderBounds(radius), 1.0f / 240.0f, banked);
        ++ticks;
        if (s.run) { ++updates; Check(s.dt >= minStep, "A capped step was below the minimum"); }
    }
    Check(ticks == 240 && updates - 1 <= 120 && updates - 1 >= 119,
          "A 240 fps second did not give about 120 steps");

    std::puts("Soft-body step model tests passed");
}
