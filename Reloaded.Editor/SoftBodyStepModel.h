#pragma once

// The soft-body step cap (RealtimeFix): which step UESoftBody::Update (110D4610) gets.
//
// Realtime Preview steps soft bodies at most 120 times a second, as the game client does: the
// cloth's constraints and damping run once per step, so stepping at the editor's frame rate
// stiffens it (#14). Each realtime frame banks its time and releases it as one step once it
// reaches the minimum; calls in between are skipped.
//
// A new simulation is the exception. Every soft-body BUILD (10F5F5F0, 10F5F990, 10F5FE60,
// 10F600A0) ends with one Update(0) through the new simulation's vtable, which the engine clamps
// to its 0.005 s minimum. That step is what gives the simulation its render bounds (the bounds
// update at vtable +0xCC, 110D74E0, writes the box at +0x28 and the sphere at +0x40). The cap
// skipped it unless the last frame happened to release a step, which a paste or a property
// change almost never meets. A simulation without bounds is never drawn, so never stepped again
// (UESoftBody's tick, 110D45B0, steps only bodies drawn in the last moments), and the map saves
// it without them: the game then puts the actor in no BSP leaf and never draws or moves it. So
// a simulation that has no bounds yet always gets its step, as in the stock editor.
//
// Maps saved by RE+ before that carry such simulations, and loading a map builds nothing. So
// before every map save, a strip door or patch whose simulation still has no bounds is given the
// build's step there (RealtimeFix::BeforeEditorCommand).
namespace SoftBodyStepModel
{
    // UESoftBody's bounding sphere radius. The constructor (110D43F0) zeroes it and only the
    // bounds update inside Update writes it.
    constexpr unsigned kBoundsRadiusOffset = 0x4C;

    // Only the constructor's exact zero means "never stepped". A cloth whose points have gone NaN
    // keeps a NaN radius (the bounds update's clamp passes NaN through) and stays under the cap.
    inline bool HasRenderBounds(float radius) { return radius != 0.0f; }

    struct Step
    {
        bool run;    // call UESoftBody::Update
        float dt;    // with this step
    };

    // A call asking for `requested`, with `banked` the step the last realtime frame released (0
    // while the bank is below the minimum).
    inline Step Choose(bool capEnabled, bool hasRenderBounds, float requested, float banked)
    {
        if (!capEnabled || !hasRenderBounds) return {true, requested};
        if (banked > 0.0f) return {true, banked};
        return {false, 0.0f};
    }

    // One realtime frame's banking: adds `seconds` to `pending` (capped at maxStep, past which the
    // engine clamps its own step anyway) and returns the step it releases, 0 below minStep.
    inline float Bank(float& pending, double seconds, float minStep, float maxStep)
    {
        pending += static_cast<float>(seconds);
        if (pending > maxStep) pending = maxStep;
        const float step = pending >= minStep ? pending : 0.0f;
        pending -= step;
        return step;
    }
}
