#pragma once

#include <Windows.h>
#include <string>

namespace BspDiagnostics
{
    void Initialize(const std::wstring& dllPath);

    // Called from the first vectored exception handler. Returns true only when
    // it captured a serious exception during an active build stage.
    bool LogException(EXCEPTION_POINTERS* exceptionInfo);
}
