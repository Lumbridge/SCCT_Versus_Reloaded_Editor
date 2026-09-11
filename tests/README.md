# Play Level regression tests

Run from the repository root in an **x86 Native Tools Command Prompt for Visual Studio**.

```bat
cl /nologo /std:c++17 /W4 /WX /EHsc tests\PlayLevelCommandTests.cpp /Fo"%TEMP%\PlayLevelCommandTests.obj" /Fe"%TEMP%\PlayLevelCommandTests.exe"
"%TEMP%\PlayLevelCommandTests.exe"

cl /nologo /std:c++14 /W4 /WX /EHsc /c tests\PlayLevelPresentationTests.cpp /Fo"%TEMP%\PlayLevelPresentationTests.obj"
cl /nologo /std:c++14 /W4 /WX /EHsc /c Reloaded.Editor\Include\d3d8to9\source\d3d8types.cpp /Fo"%TEMP%\PlayLevelPresentationConverter.obj"
link /nologo "%TEMP%\PlayLevelPresentationTests.obj" "%TEMP%\PlayLevelPresentationConverter.obj" user32.lib gdi32.lib /out:"%TEMP%\PlayLevelPresentationTests.exe"
"%TEMP%\PlayLevelPresentationTests.exe"
"%TEMP%\PlayLevelPresentationTests.exe" -ReloadedEditorPlay

cl /nologo /std:c++14 /W4 /WX /EHsc tests\PlayLevelWindowTests.cpp /Fo"%TEMP%\PlayLevelWindowTests.obj" /Fe"%TEMP%\PlayLevelWindowTests.exe"
"%TEMP%\PlayLevelWindowTests.exe"

cl /nologo /std:c++17 /W4 /WX /EHsc tests\PlayLevelConfigTests.cpp /Fo"%TEMP%\PlayLevelConfigTests.obj" /Fe"%TEMP%\PlayLevelConfigTests.exe" user32.lib
"%TEMP%\PlayLevelConfigTests.exe"
```

These tests create no windows or Direct3D devices and cannot change display modes.
The command tests preserve the captured editor map URL, game mode, team and platform
while retaining `HWND=0`. Removing `HWND=` altogether makes the game discard its
playtest command and open the normal front end.

The Reloaded launch regression keeps two quoted executable tokens in the outer
command: the launcher's token and the raw game's token. Each process removes its
own token, so `Autoplay.sdc?...` remains the first actual game argument. The
installed Reloaded launcher was also checked using an isolated synthetic child
executable and DLL: bare map arguments lose the map in the child's `WinMain`,
while the prefixed command preserves the complete URL. No game was run for that
check.

The presentation tests link the production parameter converter. Separate processes
verify ordinary launches and marked playtests, including repeated fullscreen
requests, refresh rates and swap effects. All three wrapper device creation/reset
paths use this converter. Marked requests must always become windowed, with zero
fullscreen refresh rate and a windowed-compatible presentation interval.
These converter/window tests cover the separate d3d8 wrapper; Reloaded embeds its
own renderer and uses its own borderless setting for actual Reloaded playtests.

The window tests verify enlargement, aspect ratio, frame-inclusive work-area
limits, centring on monitors with negative origins, and placement/reset reentrancy.
The renderer applies placement after the first successful presentation and after
successful resets, without repeatedly recentering an already placed window.

The configuration tests write only temporary INIs and verify actual native
viewport resolution settings, display/fixed resolution selection, original-file
preservation and error handling. No game is launched.

The Reloaded v3.0a scoreboard and overlay regressions can be checked without launching the
game or initializing its DLL. First create a patched copy from the supported
original DLL (the patcher also accepts an already patched DLL):

```bat
python tools\patch_reloaded_play_level.py "%SCCT%\Reloaded.Core.dll" --output "%TEMP%\Reloaded.Core.play-level-guard.dll"
cl /nologo /std:c++17 /W4 /WX /EHsc tests\ReloadedCorePlayLevelGuardTests.cpp /Fo"%TEMP%\ReloadedCorePlayLevelGuardTests.obj" /Fe"%TEMP%\ReloadedCorePlayLevelGuardTests.exe"
"%TEMP%\ReloadedCorePlayLevelGuardTests.exe" "%TEMP%\Reloaded.Core.play-level-guard.dll"
```

This test maps the DLL with `DONT_RESOLVE_DLL_REFERENCES` and supplies synthetic
`SPlayerProfile`, player-owner and level-context objects. Eight scoreboard cases
check null profile, null owner, eligibility, blocked state and unchanged visibility.
Eight cases for each of the overlay and controller callbacks check deferral while
required objects are absent and dispatch once they exist. Ready dispatch uses an
in-memory callee probe, avoiding graphics/UI execution; it never changes the DLL
file. Graphics modules remain unloaded throughout. All 24 cases pass.

The original DLL reproduced the first recorded crash, and the scoreboard-only
revision reproduced the overlay crash. The initialized scoreboard cases exclude
the unrelated settings-persistence call. These isolated checks do not establish
complete profile initialization or control parity.

Timed live launches under a debugger reached the selected level and accepted
keyboard input with the guards installed. Memory inspection showed that direct
Play Level startup still had no `SPlayerProfile`; normal frontend startup did
construct one. Overlay/controller deferral therefore persists in direct playtests.
Full Reloaded controls and other profile-dependent behavior remain unfinished.

End-to-end verification additionally requires a live editor playtest with both
updated editor DLL installed and the guarded Reloaded Core present: the selected level should load,
Reloaded.Core.dll should be loaded, its configured controls and frame timing should work,
the render should be sharp at the selected resolution, the window should be large
and centred, mouse input should remain correct,
and switching back to the editor should not change the desktop display mode.
