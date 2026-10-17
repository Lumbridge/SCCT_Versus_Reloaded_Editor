#pragma once
#include <optional>

// Viewport keys: which presses RE+ keeps from UUnrealEdEngine::Key (10EC8E70), and which
// Shift+letter keys a Geometric Event cannot be bound to.
//
// ViewportWndProc calls Key from WM_CHAR, the character TranslateMessage makes of every press
// and auto-repeat, and Key's modifier branches read the viewport's key-down table (which
// WM_KEYDOWN fills) rather than that character. The Ctrl branch execs BRUSH FROM DEINTERSECTION
// when D is down, so a Ctrl+D that reached Key would reshape the builder brush against the world
// (an Undo step of its own, again on every auto-repeat) as well as duplicating. Both the press
// and its character are taken in the viewport instead (GridSizeShortcut's ViewportWndProc hook):
// the first press duplicates, the auto-repeats do nothing, and the character never reaches Key,
// so D held down before Ctrl, which is in the table, does not deintersect either. Ctrl+W, the
// stock duplicate key, and Ctrl+Shift+D (Key's Shift branch duplicates) still go to Key.
namespace ViewportKeys
{
    struct Chord
    {
        unsigned key;
        bool ctrl = false, shift = false, alt = false;
        bool repeat = false; // the previous-state bit: an auto-repeat while held
    };

    enum class KeyDown { Pass, Duplicate, Swallow };

    inline KeyDown OnKeyDown(const Chord& chord)
    {
        if (chord.key != 'D' || !chord.ctrl || chord.shift || chord.alt)
            return KeyDown::Pass;
        return chord.repeat ? KeyDown::Swallow : KeyDown::Duplicate;
    }

    // A WM_CHAR: Ctrl+D's character (0x04) is kept from Key. Ctrl+Shift+D makes the same one,
    // which goes on to Key's Shift branch.
    inline KeyDown OnChar(const Chord& chord)
    {
        constexpr unsigned kCtrlD = 'D' - '@';
        return chord.key == kCtrlD && chord.ctrl && !chord.shift && !chord.alt
            ? KeyDown::Swallow : KeyDown::Pass;
    }

    // Game View is J on its own: Shift+J is the stock Select All Adjacent Surfaces.
    inline bool GameViewOnKeyUp(const Chord& chord)
    {
        return chord.key == 'J' && !chord.ctrl && !chord.shift && !chord.alt;
    }

    // WViewportFrame::OnKeyUp, where RE+ toggles Game View, gets a viewport's key-ups, and its
    // own when the frame itself has focus (a floating viewport activated by its title bar); a
    // press only passes through ViewportWndProc when the viewport has focus. A D or J key-up goes
    // by the press it ends when the viewport saw that press, so letting go of Shift before J is
    // still Shift+J, and by the keys held at the key-up when it did not.
    class KeyUps
    {
    public:
        // A viewport's WM_KEYDOWN or WM_SYSKEYDOWN; the auto-repeats keep the first press.
        void Pressed(const Chord& press)
        {
            std::optional<Chord>* seen = press.key == 'D' ? &d : press.key == 'J' ? &j : nullptr;
            if (seen && !press.repeat)
                *seen = press;
        }

        // A Ctrl+D the viewport saw was duplicated on the press, or went on to Key; one the
        // frame got itself never reached Key, so its key-up duplicates.
        bool DuplicateOnKeyUp(const Chord& keyUp)
        {
            return !Take(d) && keyUp.key == 'D' && keyUp.ctrl && !keyUp.shift && !keyUp.alt;
        }

        bool GameViewOnKeyUp(const Chord& keyUp)
        {
            const std::optional<Chord> press = Take(j);
            return ViewportKeys::GameViewOnKeyUp(press ? *press : keyUp);
        }

    private:
        static std::optional<Chord> Take(std::optional<Chord>& press)
        {
            const std::optional<Chord> taken = press;
            press.reset();
            return taken;
        }

        std::optional<Chord> d, j;
    };

    // The Shift+letter commands RE+ will not let a Geometric Event key (held with Shift while
    // clicking) share, as Key's Shift branch would run the command as well. That branch also
    // uses some of the shipped defaults (F, Z and H among them); those stay free on purpose.
    struct ReservedKey { char key; const char* action; };

    inline const ReservedKey* ReservedShiftKey(char key)
    {
        static constexpr ReservedKey kReserved[] = {
            { 'A', "Select All Actors"            },
            { 'S', "Actor Scaling Mode"           },
            { 'R', "Actor Rotate Mode"            },
            { 'D', "Duplicate"                    },
            { 'N', "Select None"                  },
            { 'J', "Select All Adjacent Surfaces" },
        };
        const char upper = key >= 'a' && key <= 'z' ? static_cast<char>(key - 'a' + 'A') : key;
        for (const auto& reserved : kReserved)
            if (reserved.key == upper)
                return &reserved;
        return nullptr;
    }
}
