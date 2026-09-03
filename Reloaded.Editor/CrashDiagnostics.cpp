#include "pch.h"
#include "CrashDiagnostics.h"

#include "logger.h"

#include <DbgHelp.h>
#include <cstdarg>
#include <cstdio>

#pragma comment(lib, "Dbghelp.lib")

namespace
{
    char g_diagnosticsDirectory[MAX_PATH] = {};

    const char* ExceptionName(DWORD code)
    {
        switch (code)
        {
        case EXCEPTION_ACCESS_VIOLATION:         return "access violation";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:   return "array bounds exceeded";
        case EXCEPTION_BREAKPOINT:              return "breakpoint";
        case EXCEPTION_DATATYPE_MISALIGNMENT:   return "datatype misalignment";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:      return "floating-point divide by zero";
        case EXCEPTION_FLT_INVALID_OPERATION:   return "invalid floating-point operation";
        case EXCEPTION_ILLEGAL_INSTRUCTION:     return "illegal instruction";
        case EXCEPTION_IN_PAGE_ERROR:           return "in-page error";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:      return "integer divide by zero";
        case EXCEPTION_STACK_OVERFLOW:          return "stack overflow";
        default:                                return "unknown";
        }
    }

    void DescribeAddress(HANDLE file, const char* label, const void* address)
    {
        HMODULE module = nullptr;
        char modulePath[MAX_PATH] = "<unmapped>";
        uintptr_t moduleOffset = 0;

        if (address && GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                    | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(address), &module))
        {
            if (!GetModuleFileNameA(module, modulePath,
                                    static_cast<DWORD>(std::size(modulePath))))
                strncpy_s(modulePath, "<module path unavailable>", _TRUNCATE);
            moduleOffset = reinterpret_cast<uintptr_t>(address)
                - reinterpret_cast<uintptr_t>(module);
        }

        CrashDiagnostics::WriteLine(
            file, "%s=%p module=%s moduleOffset=0x%08lX",
            label, address, modulePath, static_cast<unsigned long>(moduleOffset));
    }

    void WriteExceptionDetails(HANDLE file, EXCEPTION_POINTERS* exceptionInfo)
    {
        if (!exceptionInfo || !exceptionInfo->ExceptionRecord
            || !exceptionInfo->ContextRecord)
        {
            CrashDiagnostics::WriteLine(file, "Exception information unavailable");
            return;
        }

        const EXCEPTION_RECORD* record = exceptionInfo->ExceptionRecord;
        const CONTEXT* context = exceptionInfo->ContextRecord;

        CrashDiagnostics::WriteLine(
            file, "ExceptionCode=0x%08lX (%s)", record->ExceptionCode,
            ExceptionName(record->ExceptionCode));
        CrashDiagnostics::WriteLine(file, "ExceptionFlags=0x%08lX",
                                    record->ExceptionFlags);
        DescribeAddress(file, "ExceptionAddress", record->ExceptionAddress);

        if ((record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
             || record->ExceptionCode == EXCEPTION_IN_PAGE_ERROR)
            && record->NumberParameters >= 2)
        {
            const ULONG_PTR operation = record->ExceptionInformation[0];
            const char* operationName = operation == 0 ? "read"
                : operation == 1 ? "write"
                : operation == 8 ? "execute" : "unknown";
            CrashDiagnostics::WriteLine(
                file, "AccessOperation=%s AccessAddress=%p", operationName,
                reinterpret_cast<void*>(record->ExceptionInformation[1]));
        }

        CrashDiagnostics::WriteLine(
            file, "EIP=%08lX ESP=%08lX EBP=%08lX EFLAGS=%08lX",
            context->Eip, context->Esp, context->Ebp, context->EFlags);
        CrashDiagnostics::WriteLine(
            file, "EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX",
            context->Eax, context->Ebx, context->Ecx, context->Edx);
        CrashDiagnostics::WriteLine(
            file, "ESI=%08lX EDI=%08lX thread=%lu process=%lu",
            context->Esi, context->Edi, GetCurrentThreadId(),
            GetCurrentProcessId());

        CrashDiagnostics::WriteLine(file, "RawStack:");
        const DWORD* stack = reinterpret_cast<const DWORD*>(context->Esp);
        for (int index = 0; index < 64; ++index)
        {
            DWORD value = 0;
            bool readable = false;
            __try
            {
                value = stack[index];
                readable = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                readable = false;
            }
            if (!readable)
            {
                CrashDiagnostics::WriteLine(
                    file, "  stack read stopped at +0x%X", index * 4);
                break;
            }

            HMODULE module = nullptr;
            if (GetModuleHandleExA(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                        | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCSTR>(static_cast<uintptr_t>(value)),
                    &module))
            {
                char moduleName[MAX_PATH] = {};
                GetModuleFileNameA(module, moduleName,
                                   static_cast<DWORD>(std::size(moduleName)));
                const uintptr_t offset = static_cast<uintptr_t>(value)
                    - reinterpret_cast<uintptr_t>(module);
                CrashDiagnostics::WriteLine(
                    file, "  ESP+0x%03X = %08lX  %s+0x%lX", index * 4,
                    value, moduleName, static_cast<unsigned long>(offset));
            }
            else
            {
                CrashDiagnostics::WriteLine(
                    file, "  ESP+0x%03X = %08lX", index * 4, value);
            }
        }
    }
}

void CrashDiagnostics::Initialize(const std::wstring& dllPath)
{
    char narrowPath[MAX_PATH] = {};
    const int converted = WideCharToMultiByte(
        CP_ACP, 0, dllPath.c_str(), -1, narrowPath,
        static_cast<int>(std::size(narrowPath)), nullptr, nullptr);
    if (converted <= 0)
    {
        GetModuleFileNameA(nullptr, narrowPath,
                           static_cast<DWORD>(std::size(narrowPath)));
    }

    char* slash = strrchr(narrowPath, '\\');
    if (!slash)
        slash = strrchr(narrowPath, '/');
    if (slash)
        *slash = '\0';
    else
        strcpy_s(narrowPath, ".");

    _snprintf_s(g_diagnosticsDirectory, std::size(g_diagnosticsDirectory),
                _TRUNCATE, "%s\\Diagnostics", narrowPath);
    if (!CreateDirectoryA(g_diagnosticsDirectory, nullptr)
        && GetLastError() != ERROR_ALREADY_EXISTS)
    {
        Logger::log(std::string("CrashDiagnostics: could not create ")
                    + g_diagnosticsDirectory);
        g_diagnosticsDirectory[0] = '\0';
    }
}

bool CrashDiagnostics::MakeArtifactPath(
    const char* prefix, const char* extension, char* output, size_t outputSize)
{
    if (!prefix || !extension || !output || outputSize == 0
        || !g_diagnosticsDirectory[0])
        return false;

    SYSTEMTIME time = {};
    GetLocalTime(&time);
    const int result = _snprintf_s(
        output, outputSize, _TRUNCATE,
        "%s\\%s_%04u%02u%02u_%02u%02u%02u_%03u_pid%lu.%s",
        g_diagnosticsDirectory, prefix,
        time.wYear, time.wMonth, time.wDay,
        time.wHour, time.wMinute, time.wSecond, time.wMilliseconds,
        GetCurrentProcessId(), extension);
    return result > 0;
}

void CrashDiagnostics::WriteLine(HANDLE file, const char* format, ...)
{
    if (file == INVALID_HANDLE_VALUE || !format)
        return;

    char buffer[2048] = {};
    va_list args;
    va_start(args, format);
    const int length = _vsnprintf_s(
        buffer, std::size(buffer) - 3, _TRUNCATE, format, args);
    va_end(args);

    size_t bytes = length < 0 ? strlen(buffer)
                              : static_cast<size_t>(length);
    buffer[bytes++] = '\r';
    buffer[bytes++] = '\n';
    DWORD written = 0;
    WriteFile(file, buffer, static_cast<DWORD>(bytes), &written, nullptr);
}

bool CrashDiagnostics::WriteMiniDump(
    EXCEPTION_POINTERS* exceptionInfo, const char* path)
{
    if (!exceptionInfo || !path || !*path)
        return false;

    HANDLE file = CreateFileA(
        path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    MINIDUMP_EXCEPTION_INFORMATION dumpException = {};
    dumpException.ThreadId = GetCurrentThreadId();
    dumpException.ExceptionPointers = exceptionInfo;
    dumpException.ClientPointers = FALSE;

    const MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithDataSegs
        | MiniDumpWithIndirectlyReferencedMemory
        | MiniDumpWithThreadInfo
        | MiniDumpWithUnloadedModules
        | MiniDumpWithFullMemoryInfo);
    const BOOL succeeded = MiniDumpWriteDump(
        GetCurrentProcess(), GetCurrentProcessId(), file, dumpType,
        &dumpException, nullptr, nullptr);
    FlushFileBuffers(file);
    CloseHandle(file);
    if (!succeeded)
        DeleteFileA(path);
    return succeeded == TRUE;
}

void CrashDiagnostics::LogUnhandledException(EXCEPTION_POINTERS* exceptionInfo)
{
    static volatile LONG logging = 0;
    if (InterlockedCompareExchange(&logging, 1, 0) != 0)
        return;

    char reportPath[MAX_PATH] = {};
    char dumpPath[MAX_PATH] = {};
    MakeArtifactPath("EditorCrash", "log", reportPath, std::size(reportPath));
    MakeArtifactPath("EditorCrash", "dmp", dumpPath, std::size(dumpPath));

    HANDLE file = CreateFileA(
        reportPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        WriteLine(file, "SCCT Versus Reloaded Editor unhandled exception");
        WriteLine(file, "Report=%s", reportPath);
        WriteExceptionDetails(file, exceptionInfo);
        const bool dumped = exceptionInfo
            && exceptionInfo->ExceptionRecord->ExceptionCode
                != EXCEPTION_STACK_OVERFLOW
            && WriteMiniDump(exceptionInfo, dumpPath);
        WriteLine(file, "MiniDump=%s status=%s", dumpPath,
                  dumped ? "written" : "not written");
        FlushFileBuffers(file);
        CloseHandle(file);
    }

    InterlockedExchange(&logging, 0);
}
