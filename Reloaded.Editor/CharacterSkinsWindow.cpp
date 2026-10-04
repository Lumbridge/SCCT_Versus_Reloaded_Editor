#include "pch.h"
#undef min
#undef max
#include "CharacterSkinsWindow.h"
#include "CharacterSkinsModel.h"
#include "WorkflowEditor.h"
#include <string>
#include <vector>

namespace CharacterSkinsWindow
{
namespace
{
using Workflow::Json;
namespace Editor = Workflow::Editor;
// Per slot row i: edit 200+i, Use Selected 210+i, Clear 220+i.
constexpr int kEdit = 200, kUse = 210, kClear = 220, kStatus = 101, kApply = IDOK, kRemove = 102;

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
void Show(HWND window, const Json& settings)
{
    for (size_t i = 0; i < CharacterSkins::Slots.size(); ++i)
        SetDlgItemTextA(window, kEdit + static_cast<int>(i),
            settings.at("slots").value(CharacterSkins::Slots[i].property, std::string{}).c_str());
    EnableWindow(GetDlgItem(window, kRemove), settings.at("placed").get<bool>());
    std::string status = settings.at("placed").get<bool>()
        ? "This map has character skins. Change the slots and Apply, or Remove them."
        : "This map uses the stock character materials.";
    if (settings.at("extra").get<bool>()) status += " It has more than one Character Skins actor; only the first is shown.";
    Status(window, status);
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
            const int width = r.right - 24;
            Control(window, "STATIC",
                "Dress this map's spies and mercs in its own materials, for example snow camo. Select a texture in "
                "the Texture Browser and press Use Selected; an empty slot keeps the stock look.\r\n"
                "Paint over the stock textures (SPersoTextures, alpha_ATT02 and alpha_DEF01) so the UVs match: "
                "thermal and EMF vision keep the stock masks. Wet, sticky cam and phospho skins still show while "
                "they last. Import the textures into MyLevel to carry them inside the map.",
                0, kStatus + 50, 12, 10, width, 82);
            int y = 100;
            for (size_t i = 0; i < CharacterSkins::Slots.size(); ++i, y += 30)
            {
                const int id = static_cast<int>(i);
                Control(window, "STATIC", CharacterSkins::Slots[i].label, SS_CENTERIMAGE, -1, 12, y, 70, 23);
                Control(window, "EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, kEdit + id, 86, y, width - 260, 23);
                Control(window, "BUTTON", "Use Selected", WS_TABSTOP, kUse + id, width - 166, y, 100, 23);
                Control(window, "BUTTON", "Clear", WS_TABSTOP, kClear + id, width - 60, y, 72, 23);
            }
            Control(window, "STATIC", "", 0, kStatus, 12, y + 6, width, 44);
            Control(window, "BUTTON", "Remove from Map", WS_TABSTOP, kRemove, 12, r.bottom - 38, 130, 27);
            Control(window, "BUTTON", "Apply", BS_DEFPUSHBUTTON | WS_TABSTOP, kApply, r.right - 215, r.bottom - 38, 95, 27);
            Control(window, "BUTTON", "Close", WS_TABSTOP, IDCANCEL, r.right - 110, r.bottom - 38, 95, 27);
            Show(window, Editor::CharacterSkinSettings());
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
        if (id == kApply)
        {
            Json slots = Json::object();
            bool any = false;
            for (size_t i = 0; i < CharacterSkins::Slots.size(); ++i)
            {
                auto path = Text(window, kEdit + static_cast<int>(i));
                any = any || !path.empty();
                slots[CharacterSkins::Slots[i].property] = path;
            }
            if (!any)
            {
                Status(window, "Every slot is empty. Use Remove from Map to go back to the stock characters.");
                return TRUE;
            }
            Status(window, "Compiling and applying...");
            UpdateWindow(window);
            Show(window, Editor::ApplyCharacterSkins(slots));
            Status(window, "Applied. Save the map, then play it to see the skins; the editor viewports show the stock look.");
            return TRUE;
        }
        if (id == kRemove)
        {
            Editor::RemoveCharacterSkins();
            Show(window, Editor::CharacterSkinSettings());
            Status(window, "Removed. The map uses the stock character materials again.");
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
    std::vector<WORD> bytes((sizeof(DLGTEMPLATE) + 1) / 2 + 3, 0);
    auto dialog = reinterpret_cast<DLGTEMPLATE*>(bytes.data());
    dialog->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
    dialog->cx = 380;
    dialog->cy = 255;
    if (DialogBoxIndirectParamW(GetModuleHandle(nullptr), dialog, owner, Proc, 0) == -1)
        throw std::runtime_error("Cannot open the Character Skins window.");
}
} // namespace CharacterSkinsWindow
