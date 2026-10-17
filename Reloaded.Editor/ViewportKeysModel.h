#pragma once

// Viewport keys: which presses RE+ keeps from UUnrealEdEngine::Key (10EC8E70), and which
// Shift+letter keys a Geometric Event cannot be bound to.
//
// Key reads the viewport's key-down table, not only the key it is given. Its Ctrl branch execs
// BRUSH FROM DEINTERSECTION for D, so a Ctrl+D press that reached it would reshape the builder
// brush against the world (an Undo step of its own, again on every auto-repeat) as well as
// duplicating. The press is taken in the viewport instead (GridSizeShortcut's ViewportWndProc
// hook): the first one duplicates and the auto-repeats while D is held do nothing. Ctrl+W, the
// stock duplicate key, and Ctrl+Shift+D (Key's Shift branch duplicates) still go to Key.
namespace ViewportKeys
{
    struct Chord
    {
        unsigned key;
        bool ctrl = false, shift = false, alt = false;
        bool repeat = false; // WM_KEYDOWN's previous-state bit: an auto-repeat while held
    };

    enum class KeyDown { Pass, Duplicate, Swallow };

    inline KeyDown OnKeyDown(const Chord& chord)
    {
        if (chord.key != 'D' || !chord.ctrl || chord.shift || chord.alt)
            return KeyDown::Pass;
        return chord.repeat ? KeyDown::Swallow : KeyDown::Duplicate;
    }

    // Game View is J on its own: Shift+J is the stock Select All Adjacent Surfaces.
    inline bool GameViewOnKeyUp(const Chord& chord)
    {
        return chord.key == 'J' && !chord.ctrl && !chord.shift && !chord.alt;
    }

    // Key's Shift branch runs these on the press, so a Geometric Event key (held with Shift
    // while clicking) bound to one would run the command as well.
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
