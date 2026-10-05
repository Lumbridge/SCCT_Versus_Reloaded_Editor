#include "pch.h"
#undef min
#undef max
#include "MapRecoveryUi.h"
#include "WorkflowEditor.h"
#include "logger.h"
#include <commctrl.h>
#include <shellapi.h>
#include <stdexcept>
#pragma comment(lib, "comctl32.lib")

namespace MapRecoveryUi
{
namespace
{
const char kTitle[] = "Recover Compiled Map";
constexpr UINT kRefreshMessage = WM_APP + 1, kCloseMessage = WM_APP + 2;
enum : int { kStage = 100, kBar, kDetail, kReportList = 200, kReportSummary, kReportStatus, kSelect, kFrame, kFolder };

HWND Control(HWND window, const char* type, const char* text, DWORD style, int id)
{
    auto control = CreateWindowExA(0, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, window,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandle(nullptr), nullptr);
    SendMessage(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return control;
}

std::string Elapsed(DWORD milliseconds)
{
    const DWORD seconds = milliseconds / 1000;
    char text[32];
    snprintf(text, sizeof(text), "%lu:%02lu", static_cast<unsigned long>(seconds / 60), static_cast<unsigned long>(seconds % 60));
    return text;
}
}

Progress::Progress(HWND owner)
{
    if (!owner || !GetWindowRect(owner, &ownerRect_))
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &ownerRect_, 0);
    stage_ = MapRecoveryModel::Info(MapRecoveryModel::Stage::Preparing).name;
    started_ = GetTickCount();
    ready_ = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    thread_ = std::thread([this] { Run(); });
    // The window is a convenience; never hold up the recovery for it.
    if (ready_) WaitForSingleObject(ready_, 5000);
}

Progress::~Progress()
{
    // The thread signals once its window exists (or failed to); only then is
    // window_ final. A thread without a window has already returned.
    if (ready_) WaitForSingleObject(ready_, INFINITE);
    if (HWND window = window_.load()) PostMessageA(window, kCloseMessage, 0, 0);
    if (thread_.joinable()) thread_.join();
    if (ready_) CloseHandle(ready_);
}

bool Progress::Update(MapRecoveryModel::Stage stage, double fraction)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stage_ = MapRecoveryModel::Info(stage).name;
        percent_ = MapRecoveryModel::Percent(stage, fraction);
        cancelAllowed_ = MapRecoveryModel::CancelAllowed(stage);
    }
    if (HWND window = window_.load()) PostMessageA(window, kRefreshMessage, 0, 0);
    return !(cancelled_ && MapRecoveryModel::CancelAllowed(stage));
}

void Progress::Refresh()
{
    HWND window = window_.load();
    if (!window) return;
    std::string stage;
    int percent;
    bool allowed;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stage = stage_;
        percent = percent_;
        allowed = cancelAllowed_;
    }
    if (cancelled_) stage = "Cancelling after the current step: " + stage;
    SetDlgItemTextA(window, kStage, (stage + "...").c_str());
    SendDlgItemMessageA(window, kBar, PBM_SETPOS, percent, 0);
    std::string detail = std::to_string(percent) + "%  -  " + Elapsed(GetTickCount() - started_) + " elapsed";
    if (!allowed) detail += "  -  saving; this step cannot be cancelled";
    SetDlgItemTextA(window, kDetail, detail.c_str());
    EnableWindow(GetDlgItem(window, IDCANCEL), allowed && !cancelled_);
}

LRESULT CALLBACK Progress::Proc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    auto self = reinterpret_cast<Progress*>(GetWindowLongPtrA(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        self = static_cast<Progress*>(reinterpret_cast<CREATESTRUCTA*>(l)->lpCreateParams);
        SetWindowLongPtrA(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcA(window, message, w, l);
    switch (message)
    {
    case WM_CREATE:
    {
        RECT r{};
        GetClientRect(window, &r);
        const int width = r.right - 28;
        MoveWindow(Control(window, "STATIC", "", SS_LEFTNOWORDWRAP, kStage), 14, 12, width, 18, FALSE);
        MoveWindow(Control(window, PROGRESS_CLASSA, "", PBS_SMOOTH, kBar), 14, 36, width, 18, FALSE);
        SendDlgItemMessageA(window, kBar, PBM_SETRANGE32, 0, 100);
        MoveWindow(Control(window, "STATIC", "", SS_LEFTNOWORDWRAP, kDetail), 14, 60, width - 100, 18, FALSE);
        MoveWindow(Control(window, "BUTTON", "Cancel", WS_TABSTOP | BS_DEFPUSHBUTTON, IDCANCEL), r.right - 104, 76, 90, 26, FALSE);
        SetTimer(window, 1, 250, nullptr);
        return 0;
    }
    case WM_TIMER:
    case kRefreshMessage:
        self->Refresh();
        return 0;
    case WM_COMMAND:
        if (LOWORD(w) != IDCANCEL) break;
        [[fallthrough]];
    case WM_CLOSE:
    {
        bool allowed;
        {
            std::lock_guard<std::mutex> lock(self->mutex_);
            allowed = self->cancelAllowed_;
        }
        if (allowed && !self->cancelled_.exchange(true))
            Logger::log("MapRecovery: cancel requested");
        self->Refresh();
        return 0;
    }
    case kCloseMessage:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, 1);
        self->window_ = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(window, message, w, l);
}

void Progress::Run()
{
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSA wc{};
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "ReloadedRecoveryProgress";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassA(&wc);
    RECT rect{0, 0, 440, 114};
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectEx(&rect, style, FALSE, WS_EX_TOOLWINDOW);
    const int width = rect.right - rect.left, height = rect.bottom - rect.top;
    // No owner: an owner on the busy UI thread would share its input queue
    // and freeze this window with it. Topmost keeps it above the editor.
    HWND window = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, wc.lpszClassName, kTitle, style,
                                  (ownerRect_.left + ownerRect_.right - width) / 2,
                                  (ownerRect_.top + ownerRect_.bottom - height) / 2,
                                  width, height, nullptr, nullptr, wc.hInstance, this);
    window_ = window;
    if (window)
    {
        Refresh();
        ShowWindow(window, SW_SHOWNOACTIVATE);
        UpdateWindow(window);
    }
    if (ready_) SetEvent(ready_);
    if (!window) return;
    MSG message{};
    while (GetMessageA(&message, nullptr, 0, 0) > 0)
    {
        if (!IsDialogMessageA(window, &message))
        {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
    }
}

namespace
{
struct ReportWindow
{
    HWND window = nullptr;
    Report report;
    unsigned generation = 0;
    int lastRow = -1;
} shown;

void SetStatus(const std::string& text)
{
    if (IsWindow(shown.window)) SetDlgItemTextA(shown.window, kReportStatus, text.c_str());
}

void Choose(int row, bool frame)
{
    if (row < 0 || row >= static_cast<int>(shown.report.rows.size())) return;
    shown.lastRow = row;
    const auto& item = shown.report.rows[static_cast<size_t>(row)];
    if (shown.generation != Workflow::Editor::MapGeneration())
    {
        SetStatus("The recovered map is no longer the open map; reopen it to select its items.");
        return;
    }
    using MapRecoveryModel::Target;
    if (item.target == Target::None)
    {
        SetStatus(item.item + ": " + item.detail + " There is nothing in the map to select for this row.");
        return;
    }
    size_t found = 0;
    if (item.target == Target::Actor)
        found = Workflow::Editor::SelectActorNames({item.actor}, frame);
    else
        found = Workflow::Editor::SelectSurfaces({item.surface}, frame);
    if (!found)
        SetStatus(item.item + " is no longer in the map.");
    else
        SetStatus("Selected " + item.item + (frame ? " and framed it in the viewports." : "."));
}

void Layout(HWND window)
{
    RECT r{};
    GetClientRect(window, &r);
    const int w = r.right - 24, h = r.bottom;
    const int top = 104;
    MoveWindow(GetDlgItem(window, kReportSummary), 12, 8, w, top - 12, TRUE);
    MoveWindow(GetDlgItem(window, kReportList), 12, top, w, h - top - 82, TRUE);
    MoveWindow(GetDlgItem(window, kReportStatus), 12, h - 76, w, 34, TRUE);
    MoveWindow(GetDlgItem(window, kSelect), 12, h - 36, 90, 26, TRUE);
    MoveWindow(GetDlgItem(window, kFrame), 110, h - 36, 130, 26, TRUE);
    MoveWindow(GetDlgItem(window, kFolder), 248, h - 36, 160, 26, TRUE);
    MoveWindow(GetDlgItem(window, IDCANCEL), r.right - 102, h - 36, 90, 26, TRUE);
}

void Fill(HWND window)
{
    auto list = GetDlgItem(window, kReportList);
    ListView_DeleteAllItems(list);
    int row = 0;
    for (const auto& item : shown.report.rows)
    {
        LVITEMA entry{};
        entry.mask = LVIF_TEXT | LVIF_PARAM;
        entry.iItem = row;
        entry.lParam = row;
        entry.pszText = const_cast<char*>(item.category.c_str());
        SendMessageA(list, LVM_INSERTITEMA, 0, reinterpret_cast<LPARAM>(&entry));
        const std::string* cells[] = {&item.item, &item.detail};
        for (int column = 1; column <= 2; ++column)
        {
            LVITEMA cell{};
            cell.iSubItem = column;
            cell.pszText = const_cast<char*>(cells[column - 1]->c_str());
            SendMessageA(list, LVM_SETITEMTEXTA, row, reinterpret_cast<LPARAM>(&cell));
        }
        ++row;
    }
    SetDlgItemTextA(window, kReportSummary, shown.report.summary.c_str());
    SetStatus(shown.report.rows.empty()
        ? "Everything came back cleanly: no thin brushes, left-out actors or lost lighting to list. Details: " + shown.report.details
        : std::to_string(shown.report.rows.size()) + " item(s) did not come back cleanly. Click a row to select it and frame it. Details: "
              + shown.report.details);
}

LRESULT CALLBACK ReportProc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    try
    {
        switch (message)
        {
        case WM_CREATE:
        {
            Control(window, "STATIC", "", 0, kReportSummary);
            auto list = Control(window, WC_LISTVIEWA, "", WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, kReportList);
            ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            const std::pair<const char*, int> columns[] = {{"What", 170}, {"Item", 190}, {"Detail", 460}};
            for (int i = 0; i < 3; ++i)
            {
                LVCOLUMNA column{};
                column.mask = LVCF_TEXT | LVCF_WIDTH;
                column.cx = columns[i].second;
                column.pszText = const_cast<char*>(columns[i].first);
                SendMessageA(list, LVM_INSERTCOLUMNA, i, reinterpret_cast<LPARAM>(&column));
            }
            Control(window, "STATIC", "", 0, kReportStatus);
            Control(window, "BUTTON", "&Select", WS_TABSTOP, kSelect);
            Control(window, "BUTTON", "Select and &Frame", WS_TABSTOP, kFrame);
            Control(window, "BUTTON", "Open Recovery &Folder", WS_TABSTOP, kFolder);
            Control(window, "BUTTON", "Close", WS_TABSTOP, IDCANCEL);
            Layout(window);
            return 0;
        }
        case WM_SIZE:
            Layout(window);
            return 0;
        case WM_GETMINMAXINFO:
            reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize = {600, 360};
            return 0;
        case WM_NOTIFY:
        {
            auto header = reinterpret_cast<NMHDR*>(l);
            if (header->idFrom == kReportList && (header->code == NM_CLICK || header->code == NM_DBLCLK))
                Choose(reinterpret_cast<NMITEMACTIVATE*>(l)->iItem, true);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(w))
            {
            case kSelect:
            case kFrame:
            {
                const int row = ListView_GetNextItem(GetDlgItem(window, kReportList), -1, LVNI_SELECTED);
                if (row < 0) SetStatus("Choose a row first.");
                else Choose(row, LOWORD(w) == kFrame);
                return 0;
            }
            case kFolder:
                ShellExecuteA(window, "open", shown.report.folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                return 0;
            case IDCANCEL:
                DestroyWindow(window);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            shown.window = nullptr;
            return 0;
        }
    }
    catch (const std::exception& e)
    {
        // A modal box here would stall the in-editor tests; report in the status line.
        SetStatus(e.what());
        if (message == WM_CREATE) return -1;
        return 0;
    }
    return DefWindowProcA(window, message, w, l);
}
}

void ShowReport(HWND owner, Report report)
{
    shown.report = std::move(report);
    shown.generation = Workflow::Editor::MapGeneration();
    shown.lastRow = -1;
    if (IsWindow(shown.window))
    {
        Fill(shown.window);
        ShowWindow(shown.window, SW_RESTORE);
        SetForegroundWindow(shown.window);
        return;
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSA wc{};
    wc.lpfnWndProc = ReportProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "ReloadedRecoveryReport";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassA(&wc);
    shown.window = CreateWindowExA(WS_EX_TOOLWINDOW, wc.lpszClassName, "Recovery Report", WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX,
                                   CW_USEDEFAULT, CW_USEDEFAULT, 880, 560, owner, nullptr, wc.hInstance, nullptr);
    if (!shown.window)
    {
        Logger::log("MapRecovery: could not open the recovery report window");
        return;
    }
    Fill(shown.window);
    ShowWindow(shown.window, SW_SHOW);
}
}
