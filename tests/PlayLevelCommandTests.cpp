// Standalone: cl /std:c++17 /EHsc tests\PlayLevelCommandTests.cpp
#include "../Reloaded.Editor/PlayLevelCommand.h"
#include <cassert>
#include <cstdio>

int main()
{
    using PlayLevelCommand::IsEditorPlayLevelCommand;
    using PlayLevelCommand::UseOwnGameWindow;
    using PlayLevelCommand::HasConfigurationOverride;

    assert(!HasConfigurationOverride(nullptr));
    assert(!HasConfigurationOverride("Autoplay.sdc?Editeur=true HWND=0"));
    assert(!HasConfigurationOverride("map?Editeur=true?INI=other.ini"));
    assert(!HasConfigurationOverride("map -Exec=\"echo INI=other.ini\""));
    assert(HasConfigurationOverride("map INI=custom.ini"));
    assert(HasConfigurationOverride("map -ini=custom.ini"));
    assert(HasConfigurationOverride("map INI=\"C:\\Game Files\\custom.ini\""));
    assert(HasConfigurationOverride("map \"INI=custom.ini\""));

    // Captured directly from the editor's Play Level ShellExecute call.
    const char* captured = "Autoplay.sdc?GameMode=5?team=0?Editeur=true  PLATFORM=XBOX HWND=394938 -log";
    assert(IsEditorPlayLevelCommand(captured));
    assert(UseOwnGameWindow(captured)
        == "Autoplay.sdc?GameMode=5?team=0?Editeur=true  PLATFORM=XBOX HWND=0 -log");
    const std::string configured = UseOwnGameWindow(captured)
        + " -ReloadedEditorPlay INI=Reloaded_PlayLevel.ini";
    assert(IsEditorPlayLevelCommand(configured.c_str()));
    assert(HasConfigurationOverride(configured.c_str()));
    assert(configured == "Autoplay.sdc?GameMode=5?team=0?Editeur=true  PLATFORM=XBOX HWND=0 -log -ReloadedEditorPlay INI=Reloaded_PlayLevel.ini");

    const std::string launcher = "C:\\Game Files\\System\\SCCT_Versus.exe";
    const std::string game = "C:\\Game Files\\System\\SCCT Versus";
    const std::string launch = PlayLevelCommand::BuildReloadedLaunchCommand(launcher, game, configured);
    assert(launch == "\"C:\\Game Files\\System\\SCCT_Versus.exe\" \"C:\\Game Files\\System\\SCCT Versus\" " + configured);
    // Model the two executable-token removals at the process boundaries. The
    // map must remain the first actual game argument, including every option.
    size_t launchCursor = 0;
    PlayLevelCommand::Detail::Argument launchArgument;
    assert(PlayLevelCommand::Detail::NextArgument(launch, launchCursor, launchArgument));
    assert(launchArgument.value == launcher);
    assert(PlayLevelCommand::Detail::NextArgument(launch, launchCursor, launchArgument));
    assert(launchArgument.value == game);
    assert(PlayLevelCommand::Detail::NextArgument(launch, launchCursor, launchArgument));
    assert(launchArgument.value == "Autoplay.sdc?GameMode=5?team=0?Editeur=true");

    assert(IsEditorPlayLevelCommand("Autoplay.sdc?Editeur=true HWND=123"));
    assert(IsEditorPlayLevelCommand(
        "\tAutoplay.sdc?GameMode=1?eDiTeUr=TrUe?Team=0?Platform=PC\tHWND=-2147483648"));
    assert(IsEditorPlayLevelCommand("\"C:\\My Maps\\Autoplay.sdc?Editeur=true\" HWND=123"));
    assert(!IsEditorPlayLevelCommand(nullptr));
    assert(!IsEditorPlayLevelCommand(""));
    assert(!IsEditorPlayLevelCommand(" \t "));
    assert(!IsEditorPlayLevelCommand("Autoplay.sdc?Editeur=false HWND=123"));
    assert(!IsEditorPlayLevelCommand("Autoplay.sdc?NotEditeur=true HWND=123"));
    assert(!IsEditorPlayLevelCommand("Autoplay.sdc?Editeur=trueExtra HWND=123"));
    assert(!IsEditorPlayLevelCommand("Autoplay.sdc?Editeur=true=1 HWND=123"));
    assert(!IsEditorPlayLevelCommand("Autoplay.sdc?Editeur=true&Other=1 HWND=123"));
    assert(!IsEditorPlayLevelCommand("Autoplay.sdc -Exec=\"?Editeur=true\" HWND=123"));
    assert(!IsEditorPlayLevelCommand("-Exec=\"Autoplay.sdc?Editeur=true\" HWND=123"));
    assert(!IsEditorPlayLevelCommand("?Editeur=true HWND=123"));

    assert(UseOwnGameWindow("Autoplay.sdc?Editeur=true HWND=123")
        == "Autoplay.sdc?Editeur=true HWND=0");
    assert(UseOwnGameWindow("  Autoplay.sdc?Editeur=true?GameMode=1?Team=0?Platform=PC\tHWND=-2147483648  -log ")
        == "  Autoplay.sdc?Editeur=true?GameMode=1?Team=0?Platform=PC\tHWND=0  -log ");
    assert(UseOwnGameWindow("map?Editeur=true HWND=4294967295")
        == "map?Editeur=true HWND=0");
    assert(UseOwnGameWindow("map?Editeur=true hWnD=+123")
        == "map?Editeur=true HWND=0");
    assert(UseOwnGameWindow("map?Editeur=true HWND=0")
        == "map?Editeur=true HWND=0");
    assert(UseOwnGameWindow("map?Editeur=true \"HWND=-1\" HWND=2")
        == "map?Editeur=true \"HWND=0\" HWND=0");
    assert(UseOwnGameWindow("map?Editeur=true") == "map?Editeur=true HWND=0");
    assert(UseOwnGameWindow("map?Editeur=true\t") == "map?Editeur=true\tHWND=0");
    assert(UseOwnGameWindow(nullptr) == "HWND=0");
    assert(UseOwnGameWindow("") == "HWND=0");

    // Similar-looking URL options, identifiers, malformed values and command
    // text are not standalone decimal HWND arguments.
    assert(UseOwnGameWindow("map?Editeur=true?HWND=1 OtherHWND=2 -HWND=3 HWND=4suffix HWND=- HWND=")
        == "map?Editeur=true?HWND=1 OtherHWND=2 -HWND=3 HWND=4suffix HWND=- HWND= HWND=0");
    assert(UseOwnGameWindow("map?Editeur=true -Exec=\"echo HWND=123 tail\" HWND=456")
        == "map?Editeur=true -Exec=\"echo HWND=123 tail\" HWND=0");
    assert(UseOwnGameWindow("map?Editeur=true \"note HWND=123\" HWND=456")
        == "map?Editeur=true \"note HWND=123\" HWND=0");
    assert(UseOwnGameWindow(R"(map?Editeur=true -Exec="echo \" HWND=123 tail" HWND=456)")
        == R"(map?Editeur=true -Exec="echo \" HWND=123 tail" HWND=0)");
    assert(UseOwnGameWindow(R"(map?Editeur=true -path="C:\folder\\" HWND=456)")
        == R"(map?Editeur=true -path="C:\folder\\" HWND=0)");
    std::puts("Play Level command tests passed");
}
