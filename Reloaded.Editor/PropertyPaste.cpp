#include "pch.h"
#undef min
#undef max
#include "PropertyPaste.h"
#include "PropertyPasteModel.h"
#include "PropertyFilterModel.h"
#include "WorkflowEditor.h"
#include <commctrl.h>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace PropertyPaste
{
namespace
{
    using Workflow::Json;
    namespace Editor = Workflow::Editor;
    namespace Model = Workflow::PropertyPaste;
    enum : int { kFilterLabel = 100, kFilter, kList, kSummary, kTickShown, kUntickShown };

    // The copy, {actor, properties (the offered ones, in list order)}, null
    // until the first Copy Properties. confirmed holds the names last pasted
    // from it, which Paste Again pastes; a new copy starts without them.
    Json copied;
    std::set<std::string> confirmed;
    bool hasConfirmed = false;

    std::wstring Wide(const std::string& text)
    {
        if (text.empty()) return {};
        const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring wide(count, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), count);
        return wide;
    }
    std::string Narrow(const std::wstring& text)
    {
        if (text.empty()) return {};
        const int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string narrow(count, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), narrow.data(), count, nullptr, nullptr);
        return narrow;
    }

    // The selected actors a copy or paste may use: never the level info, the
    // builder brush or a camera.
    Json Selected()
    {
        Json actors = Json::array();
        for (const auto& actor : Editor::Actors(true))
            if (actor.value("authorable", false)) actors.push_back(actor);
        return actors;
    }

    // -------------------------------------------------------------- dialog

    struct Choice
    {
        const Json* properties = nullptr; // the copy's
        std::vector<bool> ticked;         // by property
        std::vector<size_t> shown;        // by list row: the property it shows
        std::wstring title;
        size_t targets = 0;
        bool filling = false;
    };
    HWND Control(HWND window, const wchar_t* type, const wchar_t* text, DWORD style, int id, int x, int y, int width, int height, DWORD extended = 0)
    {
        auto control = CreateWindowExW(extended, type, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, window,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandle(nullptr), nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        return control;
    }
    void Summary(HWND window, const Choice& choice)
    {
        const auto ticked = static_cast<size_t>(std::count(choice.ticked.begin(), choice.ticked.end(), true));
        SetDlgItemTextA(window, kSummary, Model::ChoiceSummary(ticked, choice.ticked.size(), choice.targets).c_str());
        EnableWindow(GetDlgItem(window, IDOK), ticked > 0);
    }
    // The rows the filter keeps, matched on category and name as in the
    // Properties windows. Ticks stay with their property while hidden.
    void Fill(HWND window, Choice& choice)
    {
        wchar_t text[256]{};
        GetDlgItemTextW(window, kFilter, text, 256);
        const auto words = PropertyFilter::Words(Narrow(text));
        auto list = GetDlgItem(window, kList);
        choice.filling = true;
        SendMessageW(list, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(list);
        choice.shown.clear();
        const auto& properties = *choice.properties;
        for (size_t i = 0; i < properties.size(); ++i)
        {
            const auto& entry = properties[i];
            const auto category = entry.value("category", std::string()), name = entry.at("name").get<std::string>();
            if (!PropertyFilter::Matches(category + " " + name, words)) continue;
            std::wstring values[] = { Wide(category), Wide(name), Wide(Model::ValueText(entry.at("value"))) };
            const int row = static_cast<int>(choice.shown.size());
            LVITEMW item{};
            item.mask = LVIF_TEXT | LVIF_PARAM;
            item.iItem = row;
            item.lParam = static_cast<LPARAM>(i);
            item.pszText = values[0].data();
            SendMessageW(list, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&item));
            item.mask = LVIF_TEXT;
            for (int column = 1; column < 3; ++column)
            {
                item.iSubItem = column;
                item.pszText = values[column].data();
                SendMessageW(list, LVM_SETITEMTEXTW, row, reinterpret_cast<LPARAM>(&item));
            }
            ListView_SetCheckState(list, row, choice.ticked[i] ? TRUE : FALSE);
            choice.shown.push_back(i);
        }
        SendMessageW(list, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(list, nullptr, TRUE);
        choice.filling = false;
        Summary(window, choice);
    }
    void TickShown(HWND window, Choice& choice, bool tick)
    {
        auto list = GetDlgItem(window, kList);
        choice.filling = true;
        for (size_t row = 0; row < choice.shown.size(); ++row)
        {
            choice.ticked[choice.shown[row]] = tick;
            ListView_SetCheckState(list, static_cast<int>(row), tick ? TRUE : FALSE);
        }
        choice.filling = false;
        Summary(window, choice);
    }
    INT_PTR Handle(HWND window, UINT message, WPARAM w, LPARAM l)
    {
        auto choice = reinterpret_cast<Choice*>(GetWindowLongPtr(window, DWLP_USER));
        if (message == WM_INITDIALOG)
        {
            choice = reinterpret_cast<Choice*>(l);
            SetWindowLongPtr(window, DWLP_USER, l);
            SetWindowTextW(window, choice->title.c_str());
            RECT r{};
            GetClientRect(window, &r);
            const int width = r.right - 24;
            Control(window, L"STATIC", L"Filter:", SS_CENTERIMAGE, kFilterLabel, 12, 12, 40, 22);
            Control(window, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, kFilter, 56, 12, width - 44, 22, WS_EX_CLIENTEDGE);
            auto list = Control(window, WC_LISTVIEWW, L"", WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
                                kList, 12, 44, width, r.bottom - 134);
            ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER);
            const wchar_t* columns[] = { L"Category", L"Property", L"Value" };
            const int widths[] = { 110, 160, width - 294 };
            for (int i = 0; i < 3; ++i)
            {
                LVCOLUMNW column{};
                column.mask = LVCF_TEXT | LVCF_WIDTH;
                column.cx = widths[i];
                column.pszText = const_cast<wchar_t*>(columns[i]);
                SendMessageW(list, LVM_INSERTCOLUMNW, i, reinterpret_cast<LPARAM>(&column));
            }
            Control(window, L"STATIC", L"", 0, kSummary, 12, r.bottom - 84, width, 36);
            Control(window, L"BUTTON", L"Tick Shown", WS_TABSTOP, kTickShown, 12, r.bottom - 38, 100, 27);
            Control(window, L"BUTTON", L"Untick Shown", WS_TABSTOP, kUntickShown, 118, r.bottom - 38, 100, 27);
            Control(window, L"BUTTON", L"Paste", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK, r.right - 212, r.bottom - 38, 95, 27);
            Control(window, L"BUTTON", L"Cancel", WS_TABSTOP, IDCANCEL, r.right - 107, r.bottom - 38, 95, 27);
            Fill(window, *choice);
            SetFocus(GetDlgItem(window, kFilter));
            return FALSE;
        }
        if (!choice) return FALSE;
        if (message == WM_NOTIFY)
        {
            // LVN_ITEMCHANGED has no ANSI and Unicode forms: it is what this Unicode dialog gets.
            auto header = reinterpret_cast<NMHDR*>(l);
            if (header->idFrom == kList && header->code == LVN_ITEMCHANGED && !choice->filling)
            {
                auto change = reinterpret_cast<NMLISTVIEW*>(l);
                if ((change->uChanged & LVIF_STATE) && ((change->uNewState ^ change->uOldState) & LVIS_STATEIMAGEMASK)
                    && change->iItem >= 0 && change->iItem < static_cast<int>(choice->shown.size()))
                {
                    choice->ticked[choice->shown[change->iItem]] = (change->uNewState & LVIS_STATEIMAGEMASK) == static_cast<UINT>(INDEXTOSTATEIMAGEMASK(2));
                    Summary(window, *choice);
                }
            }
            return FALSE;
        }
        if (message == WM_COMMAND)
        {
            const int id = LOWORD(w);
            if (id == kFilter && HIWORD(w) == EN_CHANGE) { Fill(window, *choice); return TRUE; }
            if (id == kTickShown || id == kUntickShown) { TickShown(window, *choice, id == kTickShown); return TRUE; }
            if (id == IDOK && std::find(choice->ticked.begin(), choice->ticked.end(), true) == choice->ticked.end()) return TRUE;
            if (id == IDOK || id == IDCANCEL) { EndDialog(window, id); return TRUE; }
        }
        if (message == WM_CLOSE) { EndDialog(window, IDCANCEL); return TRUE; }
        return FALSE;
    }
    INT_PTR CALLBACK Proc(HWND window, UINT message, WPARAM w, LPARAM l)
    {
        try { return Handle(window, message, w, l); }
        catch (const std::exception& e)
        {
            MessageBoxA(window, e.what(), "Paste Properties", MB_OK | MB_ICONERROR);
            EndDialog(window, IDCANCEL);
            return TRUE;
        }
    }
    // The copy's properties with ticks (the choice last pasted from this copy,
    // else the defaults): true with the ticked names when Paste is pressed.
    bool Choose(HWND owner, size_t targets, std::set<std::string>& names)
    {
        const auto& properties = copied.at("properties");
        Choice choice;
        choice.properties = &properties;
        choice.targets = targets;
        for (const auto& entry : properties)
            choice.ticked.push_back(hasConfirmed ? confirmed.count(entry.at("name").get<std::string>()) != 0 : Model::CheckedByDefault(entry));
        const auto path = copied.at("actor").at("path").get<std::string>();
        choice.title = Wide("Paste Properties from " + path.substr(path.find_last_of('.') + 1));
        std::vector<WORD> bytes((sizeof(DLGTEMPLATE) + 1) / 2 + 3, 0);
        auto dialog = reinterpret_cast<DLGTEMPLATE*>(bytes.data());
        dialog->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
        dialog->cx = 400;
        dialog->cy = 300;
        const auto answer = DialogBoxIndirectParamW(GetModuleHandle(nullptr), dialog, owner, Proc, reinterpret_cast<LPARAM>(&choice));
        if (answer == -1) throw std::runtime_error("Cannot open the Paste Properties window.");
        if (answer != IDOK) return false;
        names.clear();
        for (size_t i = 0; i < properties.size(); ++i)
            if (choice.ticked[i]) names.insert(properties[i].at("name").get<std::string>());
        return !names.empty();
    }

    // ------------------------------------------------------------ commands

    void Copy()
    {
        const auto actors = Selected();
        if (actors.size() != 1) throw std::runtime_error("Select the one actor to copy properties from.");
        auto copy = Editor::CopyActorProperties(actors[0]);
        copy["properties"] = Model::Offer(copy.at("properties"));
        if (copy.at("properties").empty()) throw std::runtime_error("This actor has no properties that can be pasted onto others.");
        copied = std::move(copy);
        confirmed.clear();
        hasConfirmed = false;
    }
    void Paste(HWND owner, bool again)
    {
        if (copied.is_null()) throw std::runtime_error("Right-click the actor to copy from and choose RE+: Properties > Copy Properties first.");
        const auto targets = Selected();
        if (targets.empty()) throw std::runtime_error("Select the actors to paste onto.");
        // Paste Again with no choice made from this copy yet asks for one.
        if (!again || !hasConfirmed)
        {
            std::set<std::string> names;
            if (!Choose(owner, targets.size(), names)) return;
            confirmed = std::move(names);
            hasConfirmed = true;
        }
        Json chosen = Json::array();
        for (const auto& entry : copied.at("properties"))
            if (confirmed.count(entry.at("name").get<std::string>())) chosen.push_back(entry);
        const auto result = Editor::PasteActorProperties(targets, chosen);
        const auto actors = result.at("actors").get<size_t>(), skipped = result.at("skipped").get<size_t>();
        // A paste that took everything shows in the viewports; one that left
        // values out says so.
        if (!actors || skipped)
            MessageBoxA(owner, Model::Summary(actors, result.at("properties").get<size_t>(), skipped).c_str(), "Paste Properties",
                        MB_OK | (actors ? MB_ICONINFORMATION : MB_ICONWARNING));
    }
}

bool HasCopy() { return !copied.is_null(); }

bool HandleCommand(UINT command)
{
    if (command != kCopy && command != kPaste && command != kPasteAgain) return false;
    const HWND owner = GetActiveWindow();
    try
    {
        if (command == kCopy) Copy();
        else Paste(owner, command == kPasteAgain);
    }
    catch (const std::exception& e)
    {
        MessageBoxA(owner, e.what(), command == kCopy ? "Copy Properties" : "Paste Properties", MB_OK | MB_ICONERROR);
    }
    return true;
}
}
