#pragma once

class WindowDriftFix
{
public:
    static constexpr UINT kResetPropertyWindowsCommandId = 40907;

	static void Initialize();
    static int ResetPropertyWindowPositions(HWND referenceWindow);
};
