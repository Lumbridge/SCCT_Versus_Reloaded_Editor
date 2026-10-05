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

// Saves a window's own rendering (PrintWindow) as a 24-bit BMP.
void SaveWindowBitmap(HWND window, const std::filesystem::path& path) {
    RECT rect{};
    if (!window || !GetWindowRect(window, &rect)) return;
    const int width = rect.right - rect.left, height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) return;
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    auto old = SelectObject(memory, bitmap);
    PrintWindow(window, memory, 0);
    BITMAPINFOHEADER info{sizeof(info), width, height, 1, 24, BI_RGB};
    const int stride = (width * 3 + 3) & ~3;
    std::vector<unsigned char> pixels(static_cast<size_t>(stride) * height);
    GetDIBits(memory, bitmap, 0, height, pixels.data(), reinterpret_cast<BITMAPINFO*>(&info), DIB_RGB_COLORS);
    BITMAPFILEHEADER file{0x4D42, static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + sizeof(info) + pixels.size()), 0, 0,
                          sizeof(BITMAPFILEHEADER) + sizeof(info)};
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(&file), sizeof(file));
    output.write(reinterpret_cast<const char*>(&info), sizeof(info));
    output.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
}

// The progress window lives on its own thread, so it can be captured from
// here while the recovery holds the UI thread.
DWORD WINAPI CaptureRecoveryProgress(void*) {
    for (int tick = 0; tick < 600; ++tick) {
        Sleep(100);
        if (HWND window = FindWindowA("ReloadedRecoveryProgress", nullptr)) {
            Sleep(1500);
            char stage[256] = {};
            GetDlgItemTextA(window, 100, stage, sizeof(stage));
            Record("recovery_progress_window", stage);
            SaveWindowBitmap(window, directory / "recovery_progress.bmp");
            return 0;
        }
    }
    Record("recovery_progress_window", "not seen");
    return 0;
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
    HANDLE capture = CreateThread(nullptr, 0, CaptureRecoveryProgress, nullptr, 0, nullptr);
    SendMessageA(frameWindow, WM_COMMAND, 40903, 0);
    if (capture) { WaitForSingleObject(capture, 5000); CloseHandle(capture); }
    // The report window: rows, summary, and choosing the first row.
    if (HWND report = FindWindowA("ReloadedRecoveryReport", nullptr)) {
        char text[2048] = {};
        GetDlgItemTextA(report, 201, text, sizeof(text));
        Record("recovery_report_summary", text);
        HWND list = GetDlgItem(report, 200);
        const int rows = static_cast<int>(SendMessageA(list, 0x1004 /* LVM_GETITEMCOUNT */, 0, 0));
        Record("recovery_report_rows", std::to_string(rows).c_str());
        SaveWindowBitmap(report, directory / "recovery_report.bmp");
        // The first row of each kind.
        std::set<std::string> kinds;
        for (int row = 0; row < rows; ++row) {
            struct { UINT mask; int item, subItem; UINT state, stateMask; char* text; int textMax, image; LPARAM param; } item{};
            char kind[128] = {};
            item.text = kind;
            item.textMax = sizeof(kind);
            SendMessageA(list, 0x102D /* LVM_GETITEMTEXTA */, row, reinterpret_cast<LPARAM>(&item));
            if (!kinds.insert(kind).second) continue;
            item = {};
            item.mask = 0x8; // LVIF_STATE
            item.state = item.stateMask = 0x3; // focused | selected
            SendMessageA(list, 0x102B /* LVM_SETITEMSTATE */, row, reinterpret_cast<LPARAM>(&item));
            SendMessageA(report, WM_COMMAND, 204 /* Select and Frame */, 0);
            GetDlgItemTextA(report, 202, text, sizeof(text));
            Record("recovery_report_choose", text);
        }
        SaveWindowBitmap(report, directory / "recovery_report_after.bmp");
    } else Record("recovery_report_window", "not found");
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
