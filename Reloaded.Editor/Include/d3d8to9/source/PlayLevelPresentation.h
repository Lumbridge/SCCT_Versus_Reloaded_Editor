#pragma once

#include <d3d9.h>
#include <cstring>

namespace PlayLevelPresentation
{
    inline bool IsSpace(char value)
    {
        return value == ' ' || value == '\t';
    }

    // This reserved argument is added only by the editor's Play Level launch
    // hook. Do not match occurrences inside paths, URLs or quoted command text.
    inline bool HasMarker(const char* commandLine)
    {
        if (!commandLine)
            return false;
        constexpr char marker[] = "-ReloadedEditorPlay";
        const char* cursor = commandLine;
        while (*cursor)
        {
            while (IsSpace(*cursor))
                ++cursor;
            const char* begin = cursor;
            bool quoted = false;
            size_t backslashes = 0;
            while (*cursor && (quoted || !IsSpace(*cursor)))
            {
                if (*cursor == '"' && backslashes % 2 == 0)
                    quoted = !quoted;
                backslashes = *cursor == '\\' ? backslashes + 1 : 0;
                ++cursor;
            }
            const char* end = cursor;
            if (end - begin >= 2 && *begin == '"' && end[-1] == '"' && !quoted)
            {
                ++begin;
                --end;
            }
            if (static_cast<size_t>(end - begin) == sizeof(marker) - 1
                && std::memcmp(begin, marker, sizeof(marker) - 1) == 0)
                return true;
        }
        return false;
    }

    inline bool IsEnabledForProcess()
    {
        static const bool enabled = HasMarker(GetCommandLineA());
        return enabled;
    }

    inline void Apply(D3DPRESENT_PARAMETERS& parameters, bool enabled)
    {
        if (!enabled)
            return;

        // Apply after all D3D8 conversions, including COPY_VSYNC, so none of
        // the game's later reset requests can restore exclusive fullscreen.
        parameters.Windowed = TRUE;
        parameters.FullScreen_RefreshRateInHz = 0;
        parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    }
}
