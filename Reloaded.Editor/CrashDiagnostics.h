#pragma once

#include <Windows.h>
#include <string>

namespace CrashDiagnostics
{
    // Creates System\Diagnostics beside the injected DLL. This is called once
    // during normal startup, before any exception handler needs the path.
    void Initialize(const std::wstring& dllPath);

    // Artifact paths include a timestamp and process id, so a later crash does
    // not destroy the evidence from an earlier one.
    bool MakeArtifactPath(const char* prefix, const char* extension,
                          char* output, size_t outputSize);

    // Allocation-free helpers suitable for first-chance/unhandled exception
    // paths. WriteLine deliberately does not use Logger or iostreams.
    void WriteLine(HANDLE file, const char* format, ...);
    bool WriteMiniDump(EXCEPTION_POINTERS* exceptionInfo, const char* path);
    void LogUnhandledException(EXCEPTION_POINTERS* exceptionInfo);
}
