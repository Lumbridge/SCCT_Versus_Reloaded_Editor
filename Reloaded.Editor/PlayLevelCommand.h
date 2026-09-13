#pragma once

#include <string>
#include <string_view>

namespace PlayLevelCommand
{
    // Reloaded's launcher forwards its arguments verbatim as the child's full
    // command line. Supply both executable tokens: CRT/appInit each consume
    // one at their respective process boundary, leaving the map URL intact.
    inline std::string BuildReloadedLaunchCommand(const std::string& launcherPath,
                                                 const std::string& gamePath,
                                                 const std::string& parameters)
    {
        return "\"" + launcherPath + "\" \"" + gamePath + "\" " + parameters;
    }

    namespace Detail
    {
        inline bool IsSpace(char value)
        {
            return value == ' ' || value == '\t';
        }

        inline char LowerAscii(char value)
        {
            return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
        }

        inline bool EqualsIgnoreCase(std::string_view left, std::string_view right)
        {
            if (left.size() != right.size())
                return false;
            for (size_t i = 0; i < left.size(); ++i)
            {
                if (LowerAscii(left[i]) != LowerAscii(right[i]))
                    return false;
            }
            return true;
        }

        struct Argument
        {
            size_t begin = 0;
            size_t end = 0;
            std::string_view value;
            bool wrappedInQuotes = false;
        };

        // Locate argument boundaries without rewriting their spelling. Escaped
        // quotes must not expose spaces inside unrelated quoted arguments.
        inline bool NextArgument(std::string_view command, size_t& cursor,
                                 Argument& argument)
        {
            while (cursor < command.size() && IsSpace(command[cursor]))
                ++cursor;
            if (cursor == command.size())
                return false;

            argument.begin = cursor;
            bool quoted = false;
            size_t backslashes = 0;
            while (cursor < command.size())
            {
                const char value = command[cursor];
                if (!quoted && IsSpace(value))
                    break;
                if (value == '"' && backslashes % 2 == 0)
                    quoted = !quoted;
                backslashes = value == '\\' ? backslashes + 1 : 0;
                ++cursor;
            }
            argument.end = cursor;
            argument.value = command.substr(argument.begin, cursor - argument.begin);
            argument.wrappedInQuotes = argument.value.size() >= 2
                && argument.value.front() == '"' && argument.value.back() == '"'
                && !quoted;
            if (argument.wrappedInQuotes)
                argument.value = argument.value.substr(1, argument.value.size() - 2);
            return true;
        }

        inline bool IsNumericWindowArgument(std::string_view value)
        {
            if (value.size() <= 5 || !EqualsIgnoreCase(value.substr(0, 5), "HWND="))
                return false;
            size_t digit = 5;
            if (value[digit] == '-' || value[digit] == '+')
                ++digit;
            if (digit == value.size())
                return false;
            for (; digit < value.size(); ++digit)
            {
                if (value[digit] < '0' || value[digit] > '9')
                    return false;
            }
            return true;
        }
    }

    // Stock Play Level passes the map URL first, followed by HWND=<decimal>.
    // The URL and HWND argument may each be enclosed in a pair of quotes.
    inline bool IsEditorPlayLevelCommand(const char* parameters)
    {
        if (!parameters)
            return false;
        size_t cursor = 0;
        Detail::Argument argument;
        if (!Detail::NextArgument(parameters, cursor, argument)
            || argument.value.empty() || argument.value.front() == '-')
            return false;

        size_t option = argument.value.find('?');
        if (option == 0 || option == std::string_view::npos)
            return false;
        while (option != std::string_view::npos)
        {
            const size_t next = argument.value.find('?', option + 1);
            if (Detail::EqualsIgnoreCase(argument.value.substr(option + 1,
                    next == std::string_view::npos ? next : next - option - 1),
                    "Editeur=true"))
                return true;
            option = next;
        }
        return false;
    }

    // Keep HWND= present: the game uses its presence to allow editor startup,
    // but a nonzero value makes it parent its viewport to the editor window.
    // Rewrite complete numeric arguments only; map options and quoted command
    // text remain byte-for-byte intact. No integer conversion is needed, so
    // signed 32-bit HWND values and unsigned decimal spellings both work.
    inline std::string UseOwnGameWindow(const char* parameters)
    {
        const std::string_view command = parameters ? parameters : "";
        std::string result;
        size_t cursor = 0;
        size_t copied = 0;
        bool found = false;
        Detail::Argument argument;
        while (Detail::NextArgument(command, cursor, argument))
        {
            if (!Detail::IsNumericWindowArgument(argument.value))
                continue;
            result.append(command.substr(copied, argument.begin - copied));
            result += argument.wrappedInQuotes ? "\"HWND=0\"" : "HWND=0";
            copied = argument.end;
            found = true;
        }
        result.append(command.substr(copied));
        if (!found)
        {
            if (!result.empty() && !Detail::IsSpace(result.back()))
                result += ' ';
            result += "HWND=0";
        }
        return result;
    }

    // Honour explicitly supplied native INI overrides instead of appending a
    // second one (the engine takes the first). Stock Play Level supplies none.
    inline bool HasConfigurationOverride(const char* parameters)
    {
        const std::string_view command = parameters ? parameters : "";
        size_t cursor = 0;
        Detail::Argument argument;
        while (Detail::NextArgument(command, cursor, argument))
        {
            auto value = argument.value;
            if (!value.empty() && value.front() == '-')
                value.remove_prefix(1);
            if (value.size() >= 4 && Detail::EqualsIgnoreCase(value.substr(0, 4), "INI="))
                return true;
        }
        return false;
    }
}
