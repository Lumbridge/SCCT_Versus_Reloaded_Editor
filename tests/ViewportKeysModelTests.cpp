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

    // Ctrl+D's character is kept from Key too, so D held before Ctrl does not deintersect;
    // Ctrl+Shift+D's (the same 0x04) still reaches Key's Shift branch.
    Check(OnChar({ 0x04, true }) == KeyDown::Swallow, "Ctrl+D's character reached Key");
    Check(OnChar({ 0x04, true, false, false, true }) == KeyDown::Swallow,
          "A held Ctrl+D's character reached Key");
    Check(OnChar({ 0x04, true, true }) == KeyDown::Pass, "Ctrl+Shift+D's character was taken");
    Check(OnChar({ 0x04, true, false, true }) == KeyDown::Pass, "Ctrl+Alt+D's character was taken");
    Check(OnChar({ 'd' }) == KeyDown::Pass && OnChar({ 'D', false, true }) == KeyDown::Pass,
          "A D character was taken");
    Check(OnChar({ 0x17, true }) == KeyDown::Pass, "Ctrl+W's character was taken");

    // Game View is J on its own; Shift+J is the stock Select All Adjacent Surfaces.
    Check(GameViewOnKeyUp({ 'J' }), "Plain J did not toggle Game View");
    Check(!GameViewOnKeyUp({ 'J', false, true }), "Shift+J toggled Game View");
    Check(!GameViewOnKeyUp({ 'J', true }), "Ctrl+J toggled Game View");
    Check(!GameViewOnKeyUp({ 'J', false, false, true }), "Alt+J toggled Game View");
    Check(!GameViewOnKeyUp({ 'K' }), "K toggled Game View");

    // A J key-up goes by the press the viewport saw, whichever key is let go of first.
    {
        KeyUps keyUps;
        keyUps.Pressed({ 'J', false, true });
        Check(!keyUps.GameViewOnKeyUp({ 'J' }), "Shift+J toggled Game View once Shift was let go");
        keyUps.Pressed({ 'J' });
        keyUps.Pressed({ 'J', false, true, false, true });
        Check(keyUps.GameViewOnKeyUp({ 'J', false, true }),
              "J toggled nothing when Shift went down while it was held");
        // A press the frame got itself: the keys held at the key-up decide.
        Check(keyUps.GameViewOnKeyUp({ 'J' }), "An unseen J did not toggle Game View");
        Check(!keyUps.GameViewOnKeyUp({ 'J', false, true }), "An unseen Shift+J toggled Game View");
        keyUps.Pressed({ 'K', false, true });
        Check(keyUps.GameViewOnKeyUp({ 'J' }), "A K press was taken for J's");
    }

    // A Ctrl+D key-up duplicates only for a press the viewport did not see.
    {
        KeyUps keyUps;
        keyUps.Pressed({ 'D', true });
        keyUps.Pressed({ 'D', true, false, false, true });
        Check(!keyUps.DuplicateOnKeyUp({ 'D', true }), "A Ctrl+D duplicated on its key-up as well");
        Check(keyUps.DuplicateOnKeyUp({ 'D', true }), "An unseen Ctrl+D did not duplicate");
        keyUps.Pressed({ 'D', true, false, false, true });
        Check(keyUps.DuplicateOnKeyUp({ 'D', true }),
              "An unseen Ctrl+D's auto-repeats stopped its duplicate");
        Check(!keyUps.DuplicateOnKeyUp({ 'D', true, true }), "An unseen Ctrl+Shift+D duplicated");
        Check(!keyUps.DuplicateOnKeyUp({ 'D', true, false, true }),
              "An unseen Ctrl+Alt+D duplicated");
        Check(!keyUps.DuplicateOnKeyUp({ 'D' }), "An unseen D duplicated");
        keyUps.Pressed({ 'D', true, true });
        Check(!keyUps.DuplicateOnKeyUp({ 'D', true }),
              "Ctrl+Shift+D duplicated again on its key-up");
        keyUps.Pressed({ 'D' });
        Check(!keyUps.DuplicateOnKeyUp({ 'D', true }),
              "D pressed before Ctrl duplicated on its key-up");
        keyUps.Pressed({ 'D', true });
        Check(!keyUps.DuplicateOnKeyUp({ 'D' }) && keyUps.DuplicateOnKeyUp({ 'D', true }),
              "A Ctrl+D let go of after Ctrl kept its press for the next key-up");
    }

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
