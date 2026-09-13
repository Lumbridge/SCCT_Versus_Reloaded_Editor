// Included inside the isolated probe's namespace.
std::string menuSource;

UINT_PTR CALLBACK FillRecoveryDialog(HWND window, UINT message, WPARAM, LPARAM data) {
    if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(data)->code == CDN_INITDONE) {
        HWND dialog = GetParent(window);
        SendMessageA(dialog, CDM_SETCONTROLTEXT, 1152, reinterpret_cast<LPARAM>(menuSource.c_str()));
        PostMessageA(dialog, WM_COMMAND, IDOK, 0);
    }
    return 0;
}

BOOL WINAPI SelectRecoveryFile(OPENFILENAMEA* options) {
    options->Flags |= OFN_ENABLEHOOK;
    options->lpfnHook = FillRecoveryDialog;
    return GetOpenFileNameA(options);
}

LRESULT CALLBACK AcceptRecoveryDialog(int code, WPARAM window, LPARAM data) {
    if (code == HCBT_ACTIVATE || code == HCBT_CREATEWND) {
        char className[32] = {};
        auto dialog = reinterpret_cast<HWND>(window);
        GetClassNameA(dialog, className, sizeof(className));
        if (!strcmp(className, "#32770")) PostMessageA(dialog, WM_COMMAND, IDOK, 0);
    }
    return CallNextHookEx(nullptr, code, window, data);
}

int WINAPI RecoveryMessage(HWND owner, const char* text, const char* title, UINT type) {
    Record("recovery_dialog", text);
    // Only the confirmation affects entry into recovery. Completion and error
    // text is already recorded; do not leave a background test at an OK dialog.
    if ((type & MB_TYPEMASK) == MB_OK) return IDOK;
    auto dialogHook = SetWindowsHookExA(WH_CBT, AcceptRecoveryDialog, nullptr, GetCurrentThreadId());
    int result = MessageBoxA(owner, text, title, type);
    if (dialogHook) UnhookWindowsHookEx(dialogHook);
    return result;
}

bool RecoverThroughMenu(HMODULE module, const char* source, char* destination, size_t capacity) {
    struct Replacement { void** slot; void* original; };
    std::vector<Replacement> replacements;
    auto base = reinterpret_cast<unsigned char*>(module);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base
        + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for (; imports->Name; ++imports) {
        if (!imports->OriginalFirstThunk) continue;
        auto names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imports->OriginalFirstThunk);
        auto slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imports->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name;
            void* replacement = !strcmp(reinterpret_cast<char*>(name), "GetOpenFileNameA")
                ? reinterpret_cast<void*>(&SelectRecoveryFile)
                : !strcmp(reinterpret_cast<char*>(name), "MessageBoxA")
                ? reinterpret_cast<void*>(&RecoveryMessage) : nullptr;
            if (!replacement) continue;
            auto slot = reinterpret_cast<void**>(&slots->u1.Function);
            DWORD protection;
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection)) return false;
            replacements.push_back({slot, *slot});
            *slot = replacement;
            VirtualProtect(slot, sizeof(void*), protection, &protection);
        }
    }
    menuSource = source;
    Record("recovery_menu_command", "40903");
    SendMessageA(frameWindow, WM_COMMAND, 40903, 0);
    for (const auto& entry : replacements) {
        DWORD protection;
        VirtualProtect(entry.slot, sizeof(void*), PAGE_READWRITE, &protection);
        *entry.slot = entry.original;
        VirtualProtect(entry.slot, sizeof(void*), protection, &protection);
    }
    auto levelWindow = *reinterpret_cast<unsigned char**>(0x1165E80C);
    auto filename = levelWindow ? reinterpret_cast<const char*>(levelWindow + 0x58) : "";
    if (!filename[0] || !std::filesystem::exists(filename)) return false;
    strncpy_s(destination, capacity, filename, _TRUNCATE);
    return HasGeometry();
}
