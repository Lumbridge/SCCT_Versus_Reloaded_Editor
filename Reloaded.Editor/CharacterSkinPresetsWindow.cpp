#include "pch.h"
#undef min
#undef max
#include "CharacterSkinPresetsWindow.h"
#include "CharacterSkinPresetsModel.h"
#include "WorkflowEditor.h"
#include "WorkflowTools.h"
#include <commctrl.h>
#include <commdlg.h>
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

namespace CharacterSkinPresetsWindow
{
namespace
{
using Workflow::Json;
namespace Editor = Workflow::Editor;
namespace Presets = CharacterSkins::Presets;
enum Id
{
    Search = 300, Tree, Preview, Name = 310, Category, Description, Details,
    Apply = 320, SaveCurrent, Rename, Delete, Restore, ImportFile, ExportFile, Status = 330, Hint
};
constexpr int ThumbSide = 20, Thumb = 26; // tree icons: spy body and merc body side by side
struct State
{
    HWND window{}, tree{};
    HIMAGELIST icons{};
    HFONT heading{};
    Json view, current;
    std::vector<size_t> rows; // tree item lParam >= 0: index into view.entries
    std::string selected;
    std::string previewKey; // id and modified of the pictures shown
    std::vector<CharacterSkins::Image> pictures;
    std::map<std::string, int> iconOf; // id + modified -> image list index
    bool applied = false, filling = false;
};

std::string Recode(const std::string& text, UINT from, UINT to)
{
    if (text.empty()) return {};
    int count = MultiByteToWideChar(from, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(count, L'\0');
    MultiByteToWideChar(from, 0, text.data(), static_cast<int>(text.size()), wide.data(), count);
    count = WideCharToMultiByte(to, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string out(count, '\0');
    WideCharToMultiByte(to, 0, wide.data(), static_cast<int>(wide.size()), out.data(), count, nullptr, nullptr);
    return out;
}
std::string Ansi(const std::string& utf8) { return Recode(utf8, CP_UTF8, CP_ACP); }
std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}
std::string Text(HWND window, int id)
{
    const auto item = GetDlgItem(window, id);
    std::string text(GetWindowTextLengthA(item) + 1, '\0');
    GetWindowTextA(item, text.data(), static_cast<int>(text.size()));
    text.resize(strlen(text.c_str()));
    return text;
}
void Set(State& s, int id, const std::string& text) { SetDlgItemTextA(s.window, id, text.c_str()); }
void StatusText(State& s, const std::string& text) { Set(s, Status, text); }
HWND Control(State& s, const char* type, const char* text, DWORD style, int id, int x, int y, int width, int height, DWORD ex = 0)
{
    auto control = CreateWindowExA(ex, type, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, s.window,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandle(nullptr), nullptr);
    SendMessage(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return control;
}
const Json& Entries(const State& s)
{
    static const Json empty = Json::array();
    return s.view.is_object() ? s.view.at("entries") : empty;
}
const Json* Current(const State& s)
{
    for (const auto& entry : Entries(s))
        if (entry.at("id").get<std::string>() == s.selected) return &entry;
    return nullptr;
}
bool ReadOnly(const Json& entry) { return entry.value("readonly", false); }
std::string Key(const Json& entry) { return entry.at("id").get<std::string>() + "@" + entry.value("modified", std::string{}); }

// An Image (RGBA, top-down) drawn into a rectangle.
void DrawImage(HDC dc, const CharacterSkins::Image& image, const RECT& r)
{
    if (image.width <= 0 || image.height <= 0) return;
    std::vector<std::uint8_t> bgra(image.rgba.size());
    for (size_t i = 0; i + 3 < image.rgba.size(); i += 4)
    {
        bgra[i] = image.rgba[i + 2];
        bgra[i + 1] = image.rgba[i + 1];
        bgra[i + 2] = image.rgba[i];
        bgra[i + 3] = 255;
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = image.width;
    info.bmiHeader.biHeight = -image.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchDIBits(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, 0, 0, image.width, image.height, bgra.data(), &info, DIB_RGB_COLORS, SRCCOPY);
}
HBITMAP Icon(const std::vector<CharacterSkins::Image>& thumbs)
{
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = Thumb * 2 + 2;
    info.bmiHeader.biHeight = -ThumbSide;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    auto bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) return nullptr;
    auto* pixels = static_cast<std::uint8_t*>(bits);
    // The tree's own background shows where there is no picture (the category rows).
    const auto window = GetSysColor(COLOR_WINDOW);
    for (int i = 0; i < (Thumb * 2 + 2) * ThumbSide; ++i)
    {
        pixels[i * 4] = GetBValue(window);
        pixels[i * 4 + 1] = GetGValue(window);
        pixels[i * 4 + 2] = GetRValue(window);
        pixels[i * 4 + 3] = 255;
    }
    // Spy body then merc body, each squeezed into Thumb x ThumbSide.
    for (int part = 0; part < 2; ++part)
    {
        const auto& image = thumbs[part == 0 ? 0 : 2];
        if (image.width <= 0) continue;
        const auto shrunk = Presets::Shrink(image, Thumb, ThumbSide);
        for (int y = 0; y < ThumbSide; ++y)
            for (int x = 0; x < Thumb; ++x)
            {
                const auto* p = &shrunk.rgba[(static_cast<size_t>(y) * Thumb + x) * 4];
                auto* q = &pixels[(static_cast<size_t>(y) * (Thumb * 2 + 2) + part * (Thumb + 2) + x) * 4];
                q[0] = p[2];
                q[1] = p[1];
                q[2] = p[0];
                q[3] = 255;
            }
    }
    return bitmap;
}
int IconIndex(State& s, const Json& entry)
{
    const auto key = Key(entry);
    if (auto found = s.iconOf.find(key); found != s.iconOf.end()) return found->second;
    int index = 0;
    try
    {
        auto bitmap = Icon(Editor::SkinPresetImages(entry, 64));
        if (bitmap)
        {
            index = ImageList_Add(s.icons, bitmap, nullptr);
            DeleteObject(bitmap);
        }
    }
    catch (const std::exception&) {}
    return s.iconOf[key] = std::max(index, 0);
}

void Refresh(State& s) { s.view = Editor::SkinPresets(); }
void ShowEntry(State& s);
// The tree: one bold row per category, its presets below. The search spans names,
// categories and descriptions; a category without a match is left out.
void Fill(State& s)
{
    const auto filter = Lower(Ansi(Text(s.window, Search)));
    std::vector<std::string> order;
    std::map<std::string, size_t> category;
    for (const auto& c : s.view.at("categories"))
        if (category.emplace(Workflow::Fold(c.get<std::string>()), order.size()).second) order.push_back(c.get<std::string>());
    const auto& entries = Entries(s);
    std::vector<std::vector<size_t>> rows(order.size());
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const auto& entry = entries[i];
        const auto haystack = Lower(Ansi(entry.at("name").get<std::string>() + "\n" + entry.at("category").get<std::string>() + "\n" + entry.value("description", std::string{})));
        if (!filter.empty() && haystack.find(filter) == std::string::npos) continue;
        auto c = category.find(Workflow::Fold(entry.at("category").get<std::string>()));
        if (c == category.end())
        {
            c = category.emplace(Workflow::Fold(entry.at("category").get<std::string>()), order.size()).first;
            order.push_back(entry.at("category").get<std::string>());
            rows.emplace_back();
        }
        rows[c->second].push_back(i);
    }
    s.filling = true;
    SendMessage(s.tree, WM_SETREDRAW, FALSE, 0);
    TreeView_SelectItem(s.tree, nullptr);
    TreeView_DeleteAllItems(s.tree);
    HTREEITEM select = nullptr, first = nullptr;
    size_t shown = 0;
    for (size_t c = 0; c < order.size(); ++c)
    {
        if (rows[c].empty()) continue;
        std::string label = Ansi(order[c]) + " (" + std::to_string(rows[c].size()) + ")";
        TVINSERTSTRUCTA group{};
        group.hParent = TVI_ROOT;
        group.hInsertAfter = TVI_LAST;
        group.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_STATE | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
        group.item.pszText = label.data();
        group.item.lParam = -1;
        group.item.state = group.item.stateMask = TVIS_BOLD;
        group.item.iImage = group.item.iSelectedImage = 0;
        auto parent = reinterpret_cast<HTREEITEM>(SendMessageA(s.tree, TVM_INSERTITEMA, 0, reinterpret_cast<LPARAM>(&group)));
        for (auto i : rows[c])
        {
            const auto& entry = entries[i];
            std::string name = Ansi(entry.at("name").get<std::string>()) + (ReadOnly(entry) ? "" : "  (yours)");
            TVINSERTSTRUCTA item{};
            item.hParent = parent;
            item.hInsertAfter = TVI_LAST;
            item.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
            item.item.pszText = name.data();
            item.item.lParam = static_cast<LPARAM>(i);
            item.item.iImage = item.item.iSelectedImage = IconIndex(s, entry);
            auto row = reinterpret_cast<HTREEITEM>(SendMessageA(s.tree, TVM_INSERTITEMA, 0, reinterpret_cast<LPARAM>(&item)));
            if (!first) first = row;
            if (entry.at("id").get<std::string>() == s.selected) select = row;
            ++shown;
        }
        TreeView_Expand(s.tree, parent, TVE_EXPAND);
    }
    SendMessage(s.tree, WM_SETREDRAW, TRUE, 0);
    s.filling = false;
    if (!select) select = first;
    s.selected.clear();
    if (select)
    {
        TreeView_SelectItem(s.tree, select);
        TreeView_EnsureVisible(s.tree, select);
        TVITEMA item{};
        item.mask = TVIF_PARAM;
        item.hItem = select;
        TreeView_GetItem(s.tree, &item);
        s.selected = entries[item.lParam].at("id").get<std::string>();
    }
    if (auto root = TreeView_GetRoot(s.tree)) TreeView_Select(s.tree, root, TVGN_FIRSTVISIBLE);
    ShowEntry(s);
    const auto& problems = s.view.at("problems");
    if (!shown && !filter.empty()) StatusText(s, "No preset matches the search.");
    else if (!problems.empty()) StatusText(s, Ansi(problems[0].get<std::string>()) + (problems.size() > 1 ? " (and " + std::to_string(problems.size() - 1) + " more)" : ""));
}
void Select(State& s, const std::string& id)
{
    s.selected = id;
    Refresh(s);
    for (const auto& entry : Entries(s))
        if (entry.at("id") == id && !Text(s.window, Search).empty())
        {
            const auto filter = Lower(Text(s.window, Search));
            if (Lower(Ansi(entry.at("name").get<std::string>())).find(filter) == std::string::npos) SetDlgItemTextA(s.window, Search, "");
        }
    Fill(s);
}
std::string Date(const Json& entry)
{
    long long ms = 0;
    try { ms = std::stoll(entry.value("modified", std::string("0"))); }
    catch (...) {}
    if (ms <= 0) return {};
    std::time_t t = static_cast<std::time_t>(ms / 1000);
    std::tm local{};
    if (localtime_s(&local, &t)) return {};
    char text[64]{};
    std::strftime(text, sizeof(text), "%d %b %Y %H:%M", &local);
    return text;
}
void ShowEntry(State& s)
{
    const auto* entry = Current(s);
    for (int id : {Apply, ExportFile, Delete, Rename}) EnableWindow(GetDlgItem(s.window, id), entry != nullptr);
    if (!entry)
    {
        for (int id : {Name, Category, Description, Details}) Set(s, id, "");
        s.pictures.clear();
        s.previewKey.clear();
        InvalidateRect(GetDlgItem(s.window, Preview), nullptr, TRUE);
        return;
    }
    Set(s, Name, Ansi(entry->at("name").get<std::string>()));
    Set(s, Category, Ansi(entry->at("category").get<std::string>()));
    auto description = Ansi(entry->value("description", std::string{}));
    for (size_t at = description.find('\n'); at != std::string::npos; at = description.find('\n', at + 2))
        if (!at || description[at - 1] != '\r') description.insert(at, "\r");
    Set(s, Description, description.empty() ? "No description." : description);
    std::string details = ReadOnly(*entry) ? "Built-in preset: painted over the stock textures when applied." : "Your preset";
    if (!ReadOnly(*entry))
    {
        const auto date = Date(*entry);
        if (!date.empty()) details += ", saved " + date;
        details += ".";
    }
    Set(s, Details, details + "\r\n\r\n" + Ansi(Presets::Summary(*entry)));
    EnableWindow(GetDlgItem(s.window, Rename), !ReadOnly(*entry));
    SetDlgItemTextA(s.window, Delete, ReadOnly(*entry) ? "Hide" : "Delete...");
    if (s.previewKey != Key(*entry))
    {
        SetCursor(LoadCursor(nullptr, IDC_WAIT));
        try { s.pictures = Editor::SkinPresetImages(*entry); }
        catch (const std::exception& e)
        {
            s.pictures.clear();
            StatusText(s, std::string("No preview: ") + e.what());
        }
        s.previewKey = Key(*entry);
        InvalidateRect(GetDlgItem(s.window, Preview), nullptr, TRUE);
    }
}
void PaintPreview(State& s, const DRAWITEMSTRUCT& item)
{
    const auto dc = item.hDC;
    RECT all = item.rcItem;
    auto back = CreateSolidBrush(RGB(32, 32, 32));
    FillRect(dc, &all, back);
    DeleteObject(back);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(220, 220, 220));
    SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    const int width = all.right - all.left, height = all.bottom - all.top, label = 18;
    const int side = std::max(16, std::min((width - 30) / 2, (height - 30) / 2 - label));
    for (size_t i = 0; i < CharacterSkins::Slots.size(); ++i)
    {
        const int column = static_cast<int>(i % 2), row = static_cast<int>(i / 2);
        const int x = all.left + (width - 2 * side - 10) / 2 + column * (side + 10);
        const int y = all.top + 10 + row * (side + label + 10);
        RECT caption{x, y, x + side, y + label};
        DrawTextA(dc, CharacterSkins::Slots[i].label, -1, &caption, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        RECT box{x, y + label, x + side, y + label + side};
        if (i < s.pictures.size() && s.pictures[i].width > 0) DrawImage(dc, s.pictures[i], box);
        else
        {
            auto frame = CreateSolidBrush(RGB(70, 70, 70));
            FrameRect(dc, &box, frame);
            DeleteObject(frame);
            DrawTextA(dc, "No picture (the material is not loaded)", -1, &box, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }
    }
}

std::filesystem::path PickFile(HWND owner, bool save, const std::string& suggested)
{
    wchar_t path[32768]{};
    if (save)
    {
        MultiByteToWideChar(CP_UTF8, 0, suggested.c_str(), -1, path, 32767);
    }
    OPENFILENAMEW choose{sizeof(choose)};
    choose.hwndOwner = owner;
    choose.lpstrFile = path;
    choose.nMaxFile = 32768;
    choose.lpstrFilter = L"RE+ skin presets (*.skinpreset)\0*.skinpreset\0";
    choose.lpstrDefExt = L"skinpreset";
    choose.lpstrTitle = save ? L"Export skin preset" : L"Import skin preset";
    choose.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (!(save ? GetSaveFileNameW(&choose) : GetOpenFileNameW(&choose))) return {};
    return std::filesystem::path(path);
}

// The Character Skins values to keep: the window's when it opened us, else the map's.
Json CurrentValues(const State& s)
{
    if (s.current.is_object()) return s.current;
    const auto settings = Editor::CharacterSkinSettings();
    return {{"slots", settings.at("slots")}, {"models", settings.at("models")}, {"goggles", settings.at("goggles")}};
}

void Command(State& s, int id)
{
    if (id == Apply)
    {
        const auto* entry = Current(s);
        if (!entry) throw std::runtime_error("Select a preset to apply.");
        const auto name = Ansi(entry->at("name").get<std::string>());
        StatusText(s, "Applying " + name + ": importing its pictures and compiling...");
        UpdateWindow(s.window);
        SetCursor(LoadCursor(nullptr, IDC_WAIT));
        const auto settings = Editor::ApplySkinPreset(entry->at("id").get<std::string>());
        s.current = {{"slots", settings.at("slots")}, {"models", settings.at("models")}, {"goggles", settings.at("goggles")}};
        s.applied = true;
        StatusText(s, "Applied " + name + ". Save the map, then play it to see the result; the editor viewports show the stock look.");
    }
    else if (id == SaveCurrent)
    {
        const auto values = CurrentValues(s);
        Json suggested = {{"name", "My skins"}, {"category", "Other"}, {"description", ""}};
        auto details = WorkflowTools::AskDetails(s.window, suggested, "Save current skins as a preset", s.view.at("categories"));
        if (details.is_null()) return;
        auto saved = Editor::SaveSkinPreset(values.at("slots"), values.at("models"), values.at("goggles"), details);
        Select(s, saved.at("id"));
        StatusText(s, "Saved " + Ansi(saved.at("name").get<std::string>()) + " to your presets.");
    }
    else if (id == Rename)
    {
        const auto* found = Current(s);
        if (!found) throw std::runtime_error("Select a preset first.");
        if (ReadOnly(*found)) throw std::runtime_error("Built-in presets never change. Apply one, then Save Current as Preset to keep your own copy.");
        auto changes = WorkflowTools::AskDetails(s.window, *found, "Rename preset", s.view.at("categories"));
        if (changes.is_null()) return;
        auto updated = Editor::UpdateSkinPreset(found->at("id"), changes);
        Select(s, updated.at("id"));
        StatusText(s, "Updated " + Ansi(updated.at("name").get<std::string>()) + ".");
    }
    else if (id == Delete)
    {
        const auto* found = Current(s);
        if (!found) throw std::runtime_error("Select a preset first.");
        const auto entry = *found;
        const auto name = Ansi(entry.at("name").get<std::string>());
        if (!ReadOnly(entry) &&
            MessageBoxA(s.window, ("Delete your preset '" + name + "' and its pictures? This cannot be undone.").c_str(), "Character Skin Presets", MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
            return;
        Editor::DeleteSkinPreset(entry.at("id"));
        Select(s, "");
        StatusText(s, (ReadOnly(entry) ? "Hid " + name + ". Restore Hidden brings it back." : "Deleted " + name + "."));
    }
    else if (id == Restore)
    {
        Editor::RestoreSkinPresets();
        Select(s, s.selected);
        StatusText(s, "Hidden built-in presets are listed again.");
    }
    else if (id == ExportFile)
    {
        const auto* found = Current(s);
        if (!found) throw std::runtime_error("Select a preset to export.");
        const auto file = PickFile(s.window, true, Presets::ShareFileName(found->at("name").get<std::string>()));
        if (file.empty()) return;
        SetCursor(LoadCursor(nullptr, IDC_WAIT));
        Editor::ExportSkinPreset(found->at("id"), file);
        StatusText(s, "Exported " + Ansi(found->at("name").get<std::string>()) + " to " + file.string() + ", pictures included.");
    }
    else if (id == ImportFile)
    {
        const auto file = PickFile(s.window, false, {});
        if (file.empty()) return;
        auto saved = Editor::ImportSkinPreset(file);
        Select(s, saved.at("id"));
        StatusText(s, "Imported " + Ansi(saved.at("name").get<std::string>()) + " into your presets.");
    }
}

INT_PTR CALLBACK Proc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    auto* s = reinterpret_cast<State*>(GetWindowLongPtr(window, DWLP_USER));
    try
    {
        if (message == WM_INITDIALOG)
        {
            s = reinterpret_cast<State*>(l);
            SetWindowLongPtr(window, DWLP_USER, l);
            s->window = window;
            SetWindowTextA(window, "Character Skin Presets");
            RECT r{};
            GetClientRect(window, &r);
            const int left = 300, right = 250, bottom = r.bottom - 40;
            const int mx = left + 20, mw = r.right - left - right - 40, rx = r.right - right - 8;
            Control(*s, "EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, Search, 12, 12, left - 4, 23, WS_EX_CLIENTEDGE);
            SendDlgItemMessageA(window, Search, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search presets"));
            s->tree = Control(*s, WC_TREEVIEWA, "", TVS_HASBUTTONS | TVS_SHOWSELALWAYS | TVS_NOHSCROLL | WS_TABSTOP | WS_VSCROLL, Tree, 12, 42, left - 4, bottom - 46, WS_EX_CLIENTEDGE);
            s->icons = ImageList_Create(Thumb * 2 + 2, ThumbSide, ILC_COLOR32, 16, 16);
            {
                // Index 0: a blank icon for the category rows.
                std::vector<CharacterSkins::Image> blank(4);
                auto bitmap = Icon(blank);
                ImageList_Add(s->icons, bitmap, nullptr);
                DeleteObject(bitmap);
            }
            TreeView_SetImageList(s->tree, s->icons, TVSIL_NORMAL);
            TreeView_SetItemHeight(s->tree, ThumbSide + 4);
            TreeView_SetIndent(s->tree, 12);
            Control(*s, "STATIC", "", SS_OWNERDRAW, Preview, mx, 12, mw, bottom - 40);
            Control(*s, "STATIC",
                    "Applying imports the preset's pictures into this map and compiles its Character Skins, as Apply does in the Character Skins window.",
                    0, Hint, mx, bottom - 24, mw, 30);
            s->heading = CreateFontA(-18, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
            SendMessage(Control(*s, "STATIC", "", SS_ENDELLIPSIS | SS_NOPREFIX, Name, rx, 10, right - 4, 26), WM_SETFONT, reinterpret_cast<WPARAM>(s->heading), TRUE);
            Control(*s, "STATIC", "", SS_NOPREFIX, Category, rx, 38, right - 4, 18);
            Control(*s, "STATIC", "", SS_NOPREFIX, Description, rx, 60, right - 4, 64);
            Control(*s, "STATIC", "", SS_NOPREFIX, Details, rx, 128, right - 4, bottom - 128 - 8 * 31 + 16);
            int y = bottom - 7 * 31 + 4;
            Control(*s, "BUTTON", "Apply to This Map", BS_DEFPUSHBUTTON | WS_TABSTOP, Apply, rx, y - 8, right - 4, 32);
            y += 31;
            Control(*s, "BUTTON", "Save Current as Preset...", WS_TABSTOP, SaveCurrent, rx, y, right - 4, 27);
            y += 31;
            Control(*s, "BUTTON", "Rename...", WS_TABSTOP, Rename, rx, y, (right - 10) / 2, 27);
            Control(*s, "BUTTON", "Delete...", WS_TABSTOP, Delete, rx + (right - 10) / 2 + 6, y, (right - 10) / 2, 27);
            y += 31;
            Control(*s, "BUTTON", "Restore Hidden", WS_TABSTOP, Restore, rx, y, right - 4, 27);
            y += 31;
            Control(*s, "BUTTON", "Import Preset File...", WS_TABSTOP, ImportFile, rx, y, right - 4, 27);
            y += 31;
            Control(*s, "BUTTON", "Export Preset File...", WS_TABSTOP, ExportFile, rx, y, right - 4, 27);
            Control(*s, "STATIC", "", SS_ENDELLIPSIS | SS_NOPREFIX, Status, 12, r.bottom - 32, r.right - 130, 24);
            Control(*s, "BUTTON", "Close", WS_TABSTOP, IDCANCEL, r.right - 108, r.bottom - 36, 95, 27);
            StatusText(*s, "Pick a preset to preview it; Apply to This Map dresses this map's spies and mercs in it.");
            Refresh(*s);
            Fill(*s);
            SetFocus(s->tree);
            return FALSE;
        }
        if (!s) return FALSE;
        if (message == WM_DRAWITEM && w == Preview)
        {
            PaintPreview(*s, *reinterpret_cast<DRAWITEMSTRUCT*>(l));
            return TRUE;
        }
        if (message == WM_NOTIFY)
        {
            const auto* header = reinterpret_cast<NMHDR*>(l);
            // The dialog is created with DialogBoxIndirectParamW, so the tree sends the
            // W notification; the A and W NMTREEVIEW layouts agree up to lParam.
            if (header->idFrom == Tree && (header->code == TVN_SELCHANGEDA || header->code == TVN_SELCHANGEDW) && !s->filling)
            {
                const auto* change = reinterpret_cast<NMTREEVIEWA*>(l);
                if (change->itemNew.lParam >= 0 && static_cast<size_t>(change->itemNew.lParam) < Entries(*s).size())
                {
                    s->selected = Entries(*s)[change->itemNew.lParam].at("id").get<std::string>();
                    ShowEntry(*s);
                }
                return TRUE;
            }
            if (header->idFrom == Tree && header->code == NM_DBLCLK && Current(*s))
            {
                Command(*s, Apply);
                return TRUE;
            }
            return FALSE;
        }
        if (message == WM_COMMAND)
        {
            const int id = LOWORD(w);
            if (id == Search && HIWORD(w) == EN_CHANGE)
            {
                Fill(*s);
                return TRUE;
            }
            if (id == IDCANCEL)
            {
                EndDialog(window, IDCANCEL);
                return TRUE;
            }
            if (id == IDOK) Command(*s, Apply);
            else if (HIWORD(w) == BN_CLICKED) Command(*s, id);
            return TRUE;
        }
        if (message == WM_DESTROY)
        {
            if (s->icons) ImageList_Destroy(s->icons);
            if (s->heading) DeleteObject(s->heading);
            s->icons = nullptr;
            s->heading = nullptr;
        }
    }
    catch (const std::exception& e)
    {
        MessageBeep(MB_ICONWARNING);
        if (s && s->window) StatusText(*s, Ansi(e.what()));
        return TRUE;
    }
    return FALSE;
}
} // namespace

bool Open(HWND owner, const Json& current)
{
    Editor::CharacterSkinSettings(); // fails here, before the window opens, without a map
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TREEVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    State state;
    state.current = current;
    std::vector<WORD> bytes((sizeof(DLGTEMPLATE) + 1) / 2 + 3, 0);
    auto dialog = reinterpret_cast<DLGTEMPLATE*>(bytes.data());
    dialog->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
    dialog->cx = 600;
    dialog->cy = 320;
    if (DialogBoxIndirectParamW(GetModuleHandle(nullptr), dialog, owner, Proc, reinterpret_cast<LPARAM>(&state)) == -1)
        throw std::runtime_error("Cannot open the Character Skin Presets window.");
    return state.applied;
}
} // namespace CharacterSkinPresetsWindow
