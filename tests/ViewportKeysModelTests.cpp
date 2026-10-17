#include "../Reloaded.Editor/ViewportKeysModel.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

static void Check(bool passed, const char* message)
{
    if (!passed) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

static bool Reserved(char key, const char* action)
{
    const auto* reserved = ViewportKeys::ReservedShiftKey(key);
    return reserved && std::strcmp(reserved->action, action) == 0;
}

int main()
{
    using namespace ViewportKeys;

    // Ctrl+D: the first press duplicates, the auto-repeats while D is held are kept from the
    // stock Key (its Ctrl+D is Brush From Deintersection) without duplicating again.
    Check(OnKeyDown({ 'D', true }) == KeyDown::Duplicate, "Ctrl+D did not duplicate");
    Check(OnKeyDown({ 'D', true, false, false, true }) == KeyDown::Swallow,
          "A held Ctrl+D's auto-repeat was not swallowed");

    // Everything else reaches the stock Key: Ctrl+Shift+D is its own Shift+D duplicate,
    // Ctrl+W its own duplicate key.
    Check(OnKeyDown({ 'D', true, true }) == KeyDown::Pass, "Ctrl+Shift+D was taken");
    Check(OnKeyDown({ 'D', true, false, true }) == KeyDown::Pass, "Ctrl+Alt+D was taken");
    Check(OnKeyDown({ 'D' }) == KeyDown::Pass, "Plain D was taken");
    Check(OnKeyDown({ 'D', false, false, false, true }) == KeyDown::Pass, "A plain D repeat was taken");
    Check(OnKeyDown({ 'W', true }) == KeyDown::Pass, "Ctrl+W was taken");
    Check(OnKeyDown({ 'J', true }) == KeyDown::Pass, "Ctrl+J was taken on the press");

    // Game View is J on its own; Shift+J is the stock Select All Adjacent Surfaces.
    Check(GameViewOnKeyUp({ 'J' }), "Plain J did not toggle Game View");
    Check(!GameViewOnKeyUp({ 'J', false, true }), "Shift+J toggled Game View");
    Check(!GameViewOnKeyUp({ 'J', true }), "Ctrl+J toggled Game View");
    Check(!GameViewOnKeyUp({ 'J', false, false, true }), "Alt+J toggled Game View");
    Check(!GameViewOnKeyUp({ 'K' }), "K toggled Game View");

    // Shift+letter keys the stock Key runs a command for, in either case.
    Check(Reserved('d', "Duplicate"), "Shift+D was free");
    Check(Reserved('N', "Select None"), "Shift+N was free");
    Check(Reserved('J', "Select All Adjacent Surfaces"), "Shift+J was free");
    Check(Reserved('a', "Select All Actors"), "Shift+A was free");
    Check(Reserved('s', "Actor Scaling Mode"), "Shift+S was free");
    Check(Reserved('r', "Actor Rotate Mode"), "Shift+R was free");

    // The shipped Geometric Event keys stay free.
    for (const char key : { 'E', 'H', 'P', 'L', 'Z', 'F' })
        Check(!ReservedShiftKey(key), "A shipped Geometric Event key was reserved");
    Check(!ReservedShiftKey('\0') && !ReservedShiftKey('?'), "A non-letter was reserved");

    std::puts("Viewport keys model tests passed");
}
