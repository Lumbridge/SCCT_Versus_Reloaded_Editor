#include "pch.h"
#undef min
#undef max
#include "CharacterSkinsWindow.h"
#include "CharacterPreview.h"
#include "CharacterSkinsModel.h"
#include "CharacterSkinPresetsWindow.h"
#include "WorkflowEditor.h"
#include <commdlg.h>
#include <filesystem>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <vector>
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace CharacterSkinsWindow
{
namespace
{
using Workflow::Json;
namespace Editor = Workflow::Editor;
// Per slot row i: edit 200+i, Use Selected 210+i, Clear 220+i, Import 230+i.
// Per model row m: model box 240+m, goggle offset X/Y/Z 250+3m..252+3m.
constexpr int kEdit = 200, kUse = 210, kClear = 220, kImport = 230, kModel = 240, kGoggle = 250, kStatus = 101,
              kApply = IDOK, kRemove = 102, kDownload = 103, kPreview = 104, kPreviewStatus = 105, kPresets = 106;
// Timers: the preview redraws when its view changed, and shows the fields again a
// moment after they stop changing.
constexpr UINT_PTR kDrawTimer = 1, kRefreshTimer = 2;
// Dialog width in dialog units; the fields keep the left kLeftUnits of it.
constexpr int kDialogUnits = 640, kLeftUnits = 420;
const char* const kHostClass = "ReloadedCharacterSkinsPreview";
std::wstring lastFolder;

std::filesystem::path PickFolder(HWND owner)
{
    auto initialized = OleInitialize(nullptr);
    BROWSEINFOW browse{};
    browse.hwndOwner = owner;
    browse.lpszTitle = L"Choose a folder for the default spy and merc skins (SpyBody.tga, SpyHead.tga, MercBody.tga, MercHead.tga).";
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    auto selected = SHBrowseForFolderW(&browse);
    wchar_t path[MAX_PATH]{};
    const bool picked = selected && SHGetPathFromIDListW(selected, path);
    CoTaskMemFree(selected);
    if (SUCCEEDED(initialized))
        OleUninitialize();
    return picked ? std::filesystem::path(path) : std::filesystem::path{};
}

std::filesystem::path PickImage(HWND owner, const char* slot)
{
    wchar_t path[32768]{};
    std::wstring title = L"Import an edited ";
    for (const char* c = slot; *c; ++c)
        title += static_cast<wchar_t>(*c);
    title += L" image";
    OPENFILENAMEW choose{sizeof(choose)};
    choose.hwndOwner = owner;
    choose.lpstrFile = path;
    choose.nMaxFile = 32768;
    choose.lpstrInitialDir = lastFolder.empty() ? nullptr : lastFolder.c_str();
    choose.lpstrFilter = L"Images (*.tga;*.bmp;*.pcx;*.dds)\0*.tga;*.bmp;*.pcx;*.dds\0";
    choose.lpstrTitle = title.c_str();
    choose.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&choose))
        return {};
    std::filesystem::path file(path);
    lastFolder = file.parent_path().wstring();
    return file;
}

HWND Control(HWND window, const char* type, const char* text, DWORD style, int id, int x, int y, int width, int height)
{
    auto control = CreateWindowExA(type == std::string("EDIT") ? WS_EX_CLIENTEDGE : 0, type, text,
        WS_CHILD | WS_VISIBLE | style, x, y, width, height, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandle(nullptr), nullptr);
    SendMessage(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return control;
}
std::string Text(HWND window, int id)
{
    char buffer[512]{};
    GetDlgItemTextA(window, id, buffer, sizeof(buffer));
    std::string text(buffer);
    const auto first = text.find_first_not_of(" \t"), last = text.find_last_not_of(" \t");
    return first == std::string::npos ? std::string{} : text.substr(first, last - first + 1);
}
void Status(HWND window, const std::string& text) { SetDlgItemTextA(window, kStatus, text.c_str()); }
void PreviewStatus(HWND window, const std::string& text) { SetDlgItemTextA(window, kPreviewStatus, text.c_str()); }
const char* const kPreviewHelp = "Spy on the left, merc on the right. Drag to turn, right-drag or wheel to zoom, "
                                 "middle-drag to pan, double-click to reset. Goggle lights are not shown.";
// The fields as they are now, applied or not.
Json Fields(HWND window, bool slots)
{
    Json out = Json::object();
    if (slots)
        for (size_t i = 0; i < CharacterSkins::Slots.size(); ++i)
            out[CharacterSkins::Slots[i].property] = Text(window, kEdit + static_cast<int>(i));
    else
        for (size_t m = 0; m < CharacterSkins::Models.size(); ++m)
            out[CharacterSkins::Models[m].property] = Text(window, kModel + static_cast<int>(m));
    return out;
}
// Shows the fields in the 3D preview. A field naming something the preview cannot
// show is reported under it, never in a message box; the previous look stays.
void RefreshPreview(HWND window)
{
    KillTimer(window, kRefreshTimer);
    if (!CharacterPreview::Viewport())
        return;
    try
    {
        const auto shown = CharacterPreview::Show(Fields(window, true), Fields(window, false));
        std::string text;
        for (const auto& warning : shown.at("warnings"))
            text += (text.empty() ? "" : " ") + warning.get<std::string>();
        if (text.empty())
            text = kPreviewHelp;
        PreviewStatus(window, text);
    }
    catch (const std::exception& e)
    {
        PreviewStatus(window, std::string("Not previewed: ") + e.what());
    }
}
void ScheduleRefresh(HWND window) { SetTimer(window, kRefreshTimer, 300, nullptr); }
std::string Number(double v) { return CharacterSkins::Number(v); }
void SetGoggles(HWND window, size_t model, const CharacterSkins::Offset& o)
{
    const double values[3] = {o.x, o.y, o.z};
    for (int k = 0; k < 3; ++k)
        SetDlgItemTextA(window, kGoggle + static_cast<int>(model) * 3 + k, Number(values[k]).c_str());
}
CharacterSkins::Offset Goggles(HWND window, size_t model)
{
    double values[3]{};
    for (int k = 0; k < 3; ++k)
    {
        const auto text = Text(window, kGoggle + static_cast<int>(model) * 3 + k);
        size_t used = 0;
        try { values[k] = text.empty() ? 0 : std::stod(text, &used); }
        catch (const std::exception&) { used = std::string::npos; }
        if (!text.empty() && used != text.size())
            throw std::runtime_error(std::string(CharacterSkins::Models[model].label) + ": goggle light offsets must be numbers.");
    }
    return CharacterSkins::CheckOffset({values[0], values[1], values[2]});
}
// The window's values as Apply uses them: {slots, models, goggles, any}.
Json Values(HWND window)
{
    Json slots = Json::object(), models = Json::object(), goggles = Json::object();
    bool any = false;
    for (size_t i = 0; i < CharacterSkins::Slots.size(); ++i)
    {
        auto path = Text(window, kEdit + static_cast<int>(i));
        any = any || !path.empty();
        slots[CharacterSkins::Slots[i].property] = path;
    }
    for (size_t m = 0; m < CharacterSkins::Models.size(); ++m)
    {
        const auto& model = CharacterSkins::Models[m];
        auto path = Text(window, kModel + static_cast<int>(m));
        any = any || !path.empty();
        models[model.property] = path;
        const auto o = Goggles(window, m);
        goggles[model.goggles] = Json::array({o.x, o.y, o.z});
    }
    return {{"slots", slots}, {"models", models}, {"goggles", goggles}, {"any", any}};
}
void Show(HWND window, const Json& settings)
{
    for (size_t i = 0; i < CharacterSkins::Slots.size(); ++i)
        SetDlgItemTextA(window, kEdit + static_cast<int>(i),
            settings.at("slots").value(CharacterSkins::Slots[i].property, std::string{}).c_str());
    for (size_t m = 0; m < CharacterSkins::Models.size(); ++m)
    {
        const auto& model = CharacterSkins::Models[m];
        SetDlgItemTextA(window, kModel + static_cast<int>(m), settings.at("models").value(model.property, std::string{}).c_str());
        const auto& g = settings.at("goggles").at(model.goggles);
        SetGoggles(window, m, {g[0].get<double>(), g[1].get<double>(), g[2].get<double>()});
    }
    EnableWindow(GetDlgItem(window, kRemove), settings.at("placed").get<bool>());
    std::string status = settings.at("placed").get<bool>()
        ? "This map has character skins. Change them and Apply, or Remove them."
        : "This map uses the stock characters.";
    if (settings.at("legacy").get<bool>()) status += " They were made by an earlier RE+; Apply updates them.";
    if (settings.at("extra").get<bool>()) status += " It has more than one Character Skins actor; only the first is shown.";
    Status(window, status);
    ScheduleRefresh(window);
}

INT_PTR CALLBACK Proc(HWND window, UINT message, WPARAM w, LPARAM)
{
    try
    {
        if (message == WM_INITDIALOG)
        {
            SetWindowTextA(window, "Character Skins");
            RECT r{};
            GetClientRect(window, &r);
            const int left = MulDiv(r.right, kLeftUnits, kDialogUnits), width = left - 24;
            Control(window, "STATIC",
                "Dress this map's spies and mercs in its own materials or models, for example snow camo. Download Default "
                "Skins saves the stock textures as TGA files to paint over in Photoshop or any paint program; "
                "keep their layout, since thermal and EMF vision reuse the stock masks. Import... loads an edited "
                "image into the map itself, or Use Selected takes the Texture Browser's texture. An empty slot "
                "keeps the stock look. Wet, sticky cam and phospho skins still show while they last.",
                0, kStatus + 50, 12, 10, width, 70);
            int y = 88;
            for (size_t i = 0; i < CharacterSkins::Slots.size(); ++i, y += 30)
            {
                const int id = static_cast<int>(i);
                Control(window, "STATIC", CharacterSkins::Slots[i].label, SS_CENTERIMAGE, -1, 12, y, 70, 23);
                Control(window, "EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, kEdit + id, 86, y, width - 318, 23);
                Control(window, "BUTTON", "Import...", WS_TABSTOP, kImport + id, width - 226, y, 74, 23);
                Control(window, "BUTTON", "Use Selected", WS_TABSTOP, kUse + id, width - 146, y, 92, 23);
                Control(window, "BUTTON", "Clear", WS_TABSTOP, kClear + id, width - 48, y, 60, 23);
            }
            Control(window, "STATIC",
                "Models: any loaded skeletal mesh; the team keeps its own animations. Goggle lights move "
                "by X, Y, Z along the head bone onto the new head (the merc model on a spy: 4, 0, 0).",
                0, kStatus + 51, 12, y + 4, width, 30);
            y += 38;
            const auto meshes = Editor::LoadedSkeletalMeshes();
            for (size_t m = 0; m < CharacterSkins::Models.size(); ++m, y += 30)
            {
                const int id = static_cast<int>(m);
                Control(window, "STATIC", CharacterSkins::Models[m].label, SS_CENTERIMAGE, -1, 12, y, 70, 23);
                auto box = Control(window, "COMBOBOX", "", CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP,
                                   kModel + id, 86, y, width - 318, 300);
                for (const auto& mesh : meshes)
                    SendMessageA(box, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(mesh.get<std::string>().c_str()));
                Control(window, "STATIC", "Goggle lights X Y Z", SS_CENTERIMAGE | SS_RIGHT, -1, width - 226, y, 104, 23);
                for (int k = 0; k < 3; ++k)
                    Control(window, "EDIT", "0", ES_AUTOHSCROLL | WS_TABSTOP, kGoggle + id * 3 + k, width - 116 + k * 44, y, 40, 23);
            }
            Control(window, "STATIC", "", 0, kStatus, 12, y + 6, width, 44);
            Control(window, "BUTTON", "Remove from Map", WS_TABSTOP, kRemove, 12, r.bottom - 38, 125, 27);
            Control(window, "BUTTON", "Download Default Skins...", WS_TABSTOP, kDownload, 145, r.bottom - 38, 175, 27);
            Control(window, "BUTTON", "Presets...", WS_TABSTOP, kPresets, 328, r.bottom - 38, 80, 27);
            Control(window, "BUTTON", "Apply", BS_DEFPUSHBUTTON | WS_TABSTOP, kApply, r.right - 215, r.bottom - 38, 95, 27);
            Control(window, "BUTTON", "Close", WS_TABSTOP, IDCANCEL, r.right - 110, r.bottom - 38, 95, 27);
            // The 3D preview fills the right of the window above the buttons.
            auto host = CreateWindowExA(WS_EX_CLIENTEDGE, kHostClass, "", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                                        left, 10, r.right - left - 12, r.bottom - 106, window,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreview)), GetModuleHandle(nullptr), nullptr);
            Control(window, "STATIC", kPreviewHelp, 0, kPreviewStatus, left, r.bottom - 90, r.right - left - 12, 44);
            std::string error;
            if (!host || !CharacterPreview::Attach(host, error))
                PreviewStatus(window, "No preview: " + (error.empty() ? std::string("the preview panel is not available.") : error));
            SetTimer(window, kDrawTimer, 30, nullptr);
            Show(window, Editor::CharacterSkinSettings());
            return TRUE;
        }
        if (message == WM_TIMER)
        {
            if (w == kRefreshTimer)
                RefreshPreview(window);
            else if (w == kDrawTimer)
                CharacterPreview::Tick();
            return TRUE;
        }
        if (message == WM_DESTROY)
        {
            KillTimer(window, kDrawTimer);
            KillTimer(window, kRefreshTimer);
            CharacterPreview::Detach();
            return FALSE;
        }
        if (message == WM_COMMAND && ((HIWORD(w) == EN_CHANGE && LOWORD(w) >= kEdit &&
                                       LOWORD(w) < kEdit + static_cast<int>(CharacterSkins::Slots.size())) ||
                                      ((HIWORD(w) == CBN_EDITCHANGE || HIWORD(w) == CBN_KILLFOCUS) && LOWORD(w) >= kModel &&
                                       LOWORD(w) < kModel + static_cast<int>(CharacterSkins::Models.size()))))
        {
            ScheduleRefresh(window);
            return TRUE;
        }
        if (message == WM_COMMAND && HIWORD(w) == CBN_SELCHANGE && LOWORD(w) >= kModel &&
            LOWORD(w) < kModel + static_cast<int>(CharacterSkins::Models.size()))
        {
            // Choosing a model suggests the goggle light offset seen to fit it, if there is one.
            const size_t m = LOWORD(w) - kModel;
            const auto box = GetDlgItem(window, LOWORD(w));
            const auto index = SendMessageA(box, CB_GETCURSEL, 0, 0);
            char mesh[512]{};
            if (index != CB_ERR) SendMessageA(box, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(mesh));
            const auto suggested = CharacterSkins::SuggestedGoggles(CharacterSkins::Models[m].property, mesh);
            if (suggested.x || suggested.y || suggested.z) SetGoggles(window, m, suggested);
            // The box takes its new text after this notification: preview the choice shortly.
            ScheduleRefresh(window);
            return TRUE;
        }
        if (message != WM_COMMAND || HIWORD(w) != BN_CLICKED)
            return FALSE;
        const int id = LOWORD(w);
        if (id >= kUse && id < kUse + static_cast<int>(CharacterSkins::Slots.size()))
        {
            const auto asset = Editor::CurrentAsset(false);
            if (asset.empty())
                Status(window, "Select a texture in the Texture Browser first.");
            else
                SetDlgItemTextA(window, kEdit + id - kUse, asset.c_str());
            return TRUE;
        }
        if (id >= kClear && id < kClear + static_cast<int>(CharacterSkins::Slots.size()))
        {
            SetDlgItemTextA(window, kEdit + id - kClear, "");
            return TRUE;
        }
        if (id >= kImport && id < kImport + static_cast<int>(CharacterSkins::Slots.size()))
        {
            const auto& slot = CharacterSkins::Slots[id - kImport];
            const auto file = PickImage(window, slot.label);
            if (file.empty())
                return TRUE;
            Status(window, "Importing...");
            UpdateWindow(window);
            const auto path = Editor::ImportCharacterSkin(slot.property, file);
            SetDlgItemTextA(window, kEdit + id - kImport, path.c_str());
            Status(window, "Imported as " + path + " (" + slot.format + "). Press Apply to use it, then save the map.");
            return TRUE;
        }
        if (id == kDownload)
        {
            const auto folder = PickFolder(window);
            if (folder.empty())
                return TRUE;
            std::string existing;
            for (const auto& slot : CharacterSkins::Slots)
                if (std::filesystem::exists(folder / (std::string(slot.property) + ".tga")))
                    existing += std::string(existing.empty() ? "" : ", ") + slot.property + ".tga";
            if (!existing.empty() &&
                MessageBoxA(window, (existing + " already exist in that folder. Replace them?").c_str(), "Character Skins",
                            MB_YESNO | MB_ICONQUESTION) != IDYES)
                return TRUE;
            const auto written = Editor::ExportDefaultCharacterSkins(folder);
            std::string list;
            for (const auto& file : written)
                list += std::string(list.empty() ? "" : ", ") + file.at("slot").get<std::string>() + ".tga (" +
                        std::to_string(file.at("width").get<int>()) + "x" + std::to_string(file.at("height").get<int>()) + ")";
            Status(window, "Saved " + list + ". Edit them, then Import... each one.");
            ShellExecuteW(window, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        }
        if (id == kApply)
        {
            const auto values = Values(window);
            if (!values.at("any").get<bool>())
            {
                Status(window, "Every slot is empty. Use Remove from Map to go back to the stock characters.");
                return TRUE;
            }
            Status(window, "Compiling and applying...");
            UpdateWindow(window);
            Show(window, Editor::ApplyCharacterSkins(values.at("slots"), values.at("models"), values.at("goggles")));
            Status(window, "Applied. Save the map, then play it to see the result; the map's own viewports still show the stock look.");
            return TRUE;
        }
        if (id == kPresets)
        {
            // Save Current as Preset keeps what the window shows, applied or not.
            Json values;
            try { values = Values(window); values.erase("any"); }
            catch (const std::exception&) { values = Json(); }
            if (CharacterSkinPresetsWindow::Open(window, values))
            {
                Show(window, Editor::CharacterSkinSettings());
                Status(window, "Preset applied. Save the map, then play it to see the result.");
            }
            return TRUE;
        }
        if (id == kRemove)
        {
            Editor::RemoveCharacterSkins();
            Show(window, Editor::CharacterSkinSettings());
            Status(window, "Removed. The map uses the stock characters again.");
            return TRUE;
        }
        if (id == IDCANCEL)
        {
            EndDialog(window, IDCANCEL);
            return TRUE;
        }
    }
    catch (const std::exception& e)
    {
        Status(window, e.what());
        MessageBoxA(window, e.what(), "Character Skins", MB_OK | MB_ICONERROR);
        return TRUE;
    }
    return FALSE;
}
} // namespace

void Open(HWND owner)
{
    Editor::CharacterSkinSettings(); // fails here, before the window opens, without a map
    WNDCLASSA host{};
    if (!GetClassInfoA(GetModuleHandle(nullptr), kHostClass, &host))
    {
        host.hInstance = GetModuleHandle(nullptr);
        host.lpfnWndProc = DefWindowProcA;
        host.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH));
        host.lpszClassName = kHostClass;
        RegisterClassA(&host);
    }
    std::vector<WORD> bytes((sizeof(DLGTEMPLATE) + 1) / 2 + 3, 0);
    auto dialog = reinterpret_cast<DLGTEMPLATE*>(bytes.data());
    dialog->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN | DS_MODALFRAME | DS_CENTER;
    dialog->cx = kDialogUnits;
    dialog->cy = 315;
    if (DialogBoxIndirectParamW(GetModuleHandle(nullptr), dialog, owner, Proc, 0) == -1)
        throw std::runtime_error("Cannot open the Character Skins window.");
}
} // namespace CharacterSkinsWindow
