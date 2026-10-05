#include "pch.h"
#undef min
#undef max
#include "PlacementTools.h"
#include "PlacementModel.h"
#include "MeasureTool.h"
#include "WorkflowEditor.h"
#include "GridSizeShortcutState.h"
#include "logger.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace PlacementTools
{
namespace
{
    using Workflow::Json;
    using Workflow::Vector;
    using Workflow::Rotation;
    namespace Place = Workflow::Placement;
    namespace Editor = Workflow::Editor;

    // ------------------------------------------------------------ selection

    // The order actors were selected in (the editor keeps none): looked at on
    // viewport input and before every command. The last one is the key actor.
    std::vector<uintptr_t> order;
    uintptr_t orderLevel = 0;
    DWORD lastTrack = 0;
    void Track()
    {
        try
        {
            const auto level = Editor::LevelIdentity();
            if (level != orderLevel) { order.clear(); orderLevel = level; }
            Place::UpdateOrder(order, Editor::SelectedActorAddresses());
        }
        catch (const std::exception&) { order.clear(); orderLevel = 0; }
    }

    struct Member { Json identity; Place::Box box; uintptr_t address = 0; std::string kind; };
    struct Selection
    {
        std::vector<Member> members; // in selection order, key last
        size_t Key() const { return members.size() - 1; }
    };
    Selection Selected()
    {
        Track();
        Selection selection;
        std::vector<Member> unordered;
        for (const auto& item : Editor::PlacementSelection())
        {
            Member member;
            member.identity = { { "path", item.at("path") }, { "class", item.at("class") } };
            member.box = { item.at("lo").get<Vector>(), item.at("hi").get<Vector>() };
            member.address = static_cast<uintptr_t>(item.at("address").get<uint64_t>());
            member.kind = item.at("kind").get<std::string>();
            unordered.push_back(std::move(member));
        }
        for (auto address : order)
            for (auto& member : unordered)
                if (member.address == address && address) { selection.members.push_back(member); member.address = 0; }
        for (auto& member : unordered) if (member.address) selection.members.push_back(member); // not seen being selected
        return selection;
    }
    std::vector<Place::Box> Boxes(const Selection& selection)
    {
        std::vector<Place::Box> boxes;
        for (const auto& member : selection.members) boxes.push_back(member.box);
        return boxes;
    }
    std::string Count(size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

    // -------------------------------------------------------------- reports

    // Results show for a few seconds by the mouse, in the log, and in the
    // copies window's status line when it is open; never a box to dismiss.
    HWND toast = nullptr;
    HWND dialog = nullptr;
    void SetDialogStatus(const std::string& text);
    LRESULT CALLBACK ToastProc(HWND window, UINT message, WPARAM w, LPARAM l)
    {
        if (message == WM_TIMER) { DestroyWindow(window); return 0; }
        if (message == WM_PAINT)
        {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT r{}; GetClientRect(window, &r);
            FillRect(dc, &r, GetSysColorBrush(COLOR_INFOBK));
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetSysColor(COLOR_INFOTEXT));
            auto font = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
            char text[512] = {};
            GetWindowTextA(window, text, sizeof(text));
            InflateRect(&r, -6, -4);
            DrawTextA(dc, text, -1, &r, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(dc, font);
            EndPaint(window, &paint);
            return 0;
        }
        if (message == WM_NCDESTROY) toast = nullptr;
        return DefWindowProcA(window, message, w, l);
    }
    void Toast(const std::string& text)
    {
        static bool registered = false;
        if (!registered)
        {
            WNDCLASSA wc{};
            wc.lpfnWndProc = ToastProc; wc.hInstance = GetModuleHandle(nullptr);
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = "ReloadedPlacementToast";
            registered = RegisterClassA(&wc) != 0;
        }
        if (toast) DestroyWindow(toast);
        HDC dc = GetDC(nullptr);
        auto font = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
        RECT measure{ 0, 0, 360, 0 };
        DrawTextA(dc, text.c_str(), -1, &measure, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, font); ReleaseDC(nullptr, dc);
        POINT at{}; GetCursorPos(&at);
        toast = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, "ReloadedPlacementToast", text.c_str(), WS_POPUP | WS_BORDER,
                                at.x + 16, at.y + 20, measure.right + 14, measure.bottom + 10, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
        if (!toast) return;
        ShowWindow(toast, SW_SHOWNOACTIVATE);
        SetTimer(toast, 1, 3500, nullptr);
    }
    void Report(const std::string& text, bool problem = false)
    {
        Logger::log("Placement: " + text);
        if (problem) MessageBeep(MB_ICONWARNING);
        Toast(text);
    }

    // ------------------------------------------------------ instant commands

    const char* AxisName(int axis) { return axis == 0 ? "X" : axis == 1 ? "Y" : "Z"; }
    std::string Skipped(size_t locked)
    {
        return locked ? " " + Count(locked, "locked actor stays", "locked actors stay") + " where it is." : "";
    }
    size_t Move(const Selection& selection, const std::vector<Vector>& deltas, const std::string& label)
    {
        Json moves = Json::array();
        for (size_t i = 0; i < selection.members.size(); ++i)
        {
            auto move = selection.members[i].identity;
            move["delta"] = deltas[i];
            moves.push_back(move);
        }
        return Editor::PlacementMove(moves, label);
    }
    size_t Movable(const std::vector<Vector>& deltas)
    {
        size_t n = 0;
        for (const auto& d : deltas) if (Place::Length(d) >= 0.001) ++n;
        return n;
    }
    void Align(int axis, Place::Edge edge)
    {
        const auto selection = Selected();
        if (selection.members.size() < 2) throw std::runtime_error("Select two or more actors; the last one you select stays put and the others line up with it.");
        const auto deltas = Place::AlignDeltas(Boxes(selection), selection.Key(), axis, edge);
        const char* edgeName = edge == Place::Edge::Min ? "minimum" : edge == Place::Edge::Max ? "maximum" : "centre";
        const auto wanted = Movable(deltas);
        const auto moved = Move(selection, deltas, std::string("Align ") + AxisName(axis) + " " + edgeName);
        const auto key = selection.members.back().identity.at("path").get<std::string>();
        if (!wanted) Report(std::string("Already aligned on ") + AxisName(axis) + ".");
        else Report("Aligned " + Count(moved, "actor's", "actors'") + " " + AxisName(axis) + " " + edgeName + " to " + key.substr(key.find('.') + 1) + "." + Skipped(wanted - moved));
    }
    void Distribute(int axis)
    {
        const auto selection = Selected();
        if (selection.members.size() < 3) throw std::runtime_error("Select three or more actors to distribute.");
        std::vector<Vector> deltas;
        std::string label;
        if (axis >= 0) { deltas = Place::DistributeAxisDeltas(Boxes(selection), axis); label = std::string("Distribute along ") + AxisName(axis); }
        else { deltas = Place::DistributeLineDeltas(Boxes(selection), 0, selection.Key()); label = "Distribute between first and last selected"; }
        const auto wanted = Movable(deltas);
        const auto moved = Move(selection, deltas, label);
        if (!wanted) Report("Already evenly spaced.");
        else Report(label + ": moved " + Count(moved, "actor.", "actors.") + Skipped(wanted - moved));
    }
    void Drop(Place::Surface surface, bool align)
    {
        Track();
        const auto report = Editor::PlacementDrop(static_cast<int>(surface), align);
        const int moved = report.at("moved"), missed = report.at("missed"), unchanged = report.at("unchanged"), locked = report.at("locked"), brushes = report.at("brushes");
        std::string text = moved ? std::string(surface == Place::Surface::Wall ? "Pushed " : "Dropped ") + Count(moved, "actor", "actors") + " to the " + Place::SurfaceName(surface) + "."
                                 : std::string("Nothing moved.");
        if (unchanged) text += " " + Count(unchanged, "was", "were") + " already there.";
        if (missed) text += " No " + std::string(Place::SurfaceName(surface)) + " found for " + Count(missed, "actor.", "actors.");
        if (locked) text += " " + Count(locked, "locked actor stays", "locked actors stay") + " put.";
        if (brushes) text += " " + Count(brushes, "CSG brush was", "CSG brushes were") + " skipped: a brush would only find its own geometry.";
        Report(text, !moved);
    }

    // --------------------------------------------------------------- copies

    enum class Mode { Linear, Radial, Path };
    enum class Anchor { Builder, Key, Point };
    enum : int
    {
        kModeLinear = 100, kModeRadial, kModePath, kCount, kOffsetX, kOffsetY, kOffsetZ, kBoxSize,
        kAnchor, kPointX, kPointY, kPointZ, kArc, kRadius, kTurn, kBySpacing, kSpacing, kPreview,
        kStatus, kCreate, kClose
    };
    constexpr UINT_PTR kDialogTimer = 1;
    bool updating = false;  // fields written by the window itself
    bool previewPaused = false; // after Create, until something is edited
    std::string sticky;         // a result or error shown until something is edited
    std::vector<MeasureTool::ToolLine> shownLines;
    unsigned dialogLevelGeneration = 0;

    HWND Item(int id) { return GetDlgItem(dialog, id); }
    bool Checked(int id) { return SendMessageA(Item(id), BM_GETCHECK, 0, 0) == BST_CHECKED; }
    void SetChecked(int id, bool on) { SendMessageA(Item(id), BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0); }
    std::string Text(int id)
    {
        char text[128] = {};
        GetWindowTextA(Item(id), text, sizeof(text));
        return text;
    }
    void SetText(int id, const std::string& text) { updating = true; SetWindowTextA(Item(id), text.c_str()); updating = false; }
    double Number(int id, const char* what)
    {
        const auto text = Text(id);
        char* end = nullptr;
        const double value = std::strtod(text.c_str(), &end);
        while (end && *end == ' ') ++end;
        if (text.empty() || !end || *end || !std::isfinite(value)) throw std::runtime_error(std::string("Enter a number for ") + what + ".");
        return value;
    }
    int Whole(int id, const char* what)
    {
        const double value = Number(id, what);
        if (value != std::floor(value) || value < 0 || value > 100000) throw std::runtime_error(std::string("Enter a whole number for ") + what + ".");
        return static_cast<int>(value);
    }
    std::string Format(double value)
    {
        char text[64];
        snprintf(text, sizeof(text), "%.6g", std::abs(value) < 0.005 ? 0.0 : value);
        return text;
    }
    void SetDialogStatus(const std::string& text) { if (dialog) SetWindowTextA(Item(kStatus), text.c_str()); }
    Mode CurrentMode() { return Checked(kModeRadial) ? Mode::Radial : Checked(kModePath) ? Mode::Path : Mode::Linear; }
    Anchor CurrentAnchor()
    {
        const auto choice = SendMessageA(Item(kAnchor), CB_GETCURSEL, 0, 0);
        return choice == 1 ? Anchor::Key : choice == 2 ? Anchor::Point : Anchor::Builder;
    }

    // What Create would make: the members to copy and one Copy each.
    struct CopyPlan
    {
        std::vector<Member> members;
        std::vector<Place::Copy> copies;
        Vector anchor{};
        bool hasAnchor = false;
        std::string label, summary;
    };
    CopyPlan Plan()
    {
        const auto selection = Selected();
        if (selection.members.empty()) throw std::runtime_error("Select the actors to copy.");
        const auto mode = CurrentMode();
        CopyPlan plan;
        plan.members = selection.members;
        if (mode != Mode::Linear)
        {
            const auto anchor = CurrentAnchor();
            if (anchor == Anchor::Key)
            {
                if (plan.members.size() < 2) throw std::runtime_error("Select what to copy, then the actor to copy " + std::string(mode == Mode::Radial ? "round" : "towards") + " last.");
                plan.anchor = Place::Centre(plan.members.back().box);
                plan.members.pop_back();
            }
            else if (anchor == Anchor::Builder) plan.anchor = Editor::BuilderPose().position;
            else plan.anchor = { Number(kPointX, "the point's X"), Number(kPointY, "the point's Y"), Number(kPointZ, "the point's Z") };
            plan.hasAnchor = true;
            if (anchor != Anchor::Point)
            {
                SetText(kPointX, Format(plan.anchor[0])); SetText(kPointY, Format(plan.anchor[1])); SetText(kPointZ, Format(plan.anchor[2]));
            }
        }
        std::vector<Place::Box> boxes;
        for (const auto& member : plan.members) boxes.push_back(member.box);
        const auto centre = Place::Centre(Place::Union(boxes));
        if (mode == Mode::Linear)
        {
            plan.copies = Place::Linear(Whole(kCount, "copies"), { Number(kOffsetX, "the X offset"), Number(kOffsetY, "the Y offset"), Number(kOffsetZ, "the Z offset") });
            plan.label = "Copies in a line";
        }
        else if (mode == Mode::Radial)
        {
            plan.copies = Place::Radial(Whole(kCount, "copies"), centre, plan.anchor, Number(kArc, "the arc"), Number(kRadius, "the radius"), Checked(kTurn));
            plan.label = "Copies round a centre";
        }
        else
        {
            const bool bySpacing = Checked(kBySpacing);
            plan.copies = Place::Path(centre, plan.anchor, bySpacing ? 0 : Whole(kCount, "copies"), bySpacing ? Number(kSpacing, "the spacing") : 0);
            plan.label = "Copies along a path";
        }
        Place::CheckCount(static_cast<int>(plan.copies.size()), plan.members.size());
        plan.summary = Count(plan.copies.size(), "copy", "copies") + " of " + Count(plan.members.size(), "actor", "actors")
                     + " (" + std::to_string(plan.copies.size() * plan.members.size()) + " new).";
        return plan;
    }

    // The copies' boxes and the centre or end point, drawn into the viewports.
    constexpr uint32_t kPreviewColour = 0xFF40E0FF, kAnchorColour = 0xFFFF9020;
    constexpr size_t kPreviewLines = 6000;
    void ShowLines(std::vector<MeasureTool::ToolLine> lines)
    {
        auto same = [](const std::vector<MeasureTool::ToolLine>& a, const std::vector<MeasureTool::ToolLine>& b)
        {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (a[i].from != b[i].from || a[i].to != b[i].to || a[i].colour != b[i].colour) return false;
            return true;
        };
        if (same(lines, shownLines)) return;
        shownLines = lines;
        MeasureTool::SetToolLines(std::move(lines));
        try { Editor::Redraw(); } catch (const std::exception&) {}
    }
    void Preview()
    {
        if (!dialog) return;
        std::vector<MeasureTool::ToolLine> lines;
        try
        {
            const auto plan = Plan();
            if (Checked(kPreview) && !previewPaused)
            {
                for (const auto& copy : plan.copies)
                {
                    for (const auto& member : plan.members)
                    {
                        // A point-sized box (a light with no collision) still shows as a small cross.
                        auto box = member.box;
                        for (int axis = 0; axis < 3; ++axis) if (box.hi[axis] - box.lo[axis] < 8) { const double mid = (box.lo[axis] + box.hi[axis]) / 2; box.lo[axis] = mid - 4; box.hi[axis] = mid + 4; }
                        for (const auto& [from, to] : Place::BoxEdges(box, copy)) lines.push_back({ from, to, kPreviewColour });
                        if (lines.size() >= kPreviewLines) break;
                    }
                    if (lines.size() >= kPreviewLines) break;
                }
                if (plan.hasAnchor)
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        Vector a = plan.anchor, b = plan.anchor;
                        a[axis] -= 32; b[axis] += 32;
                        lines.push_back({ a, b, kAnchorColour });
                    }
            }
            const bool cut = lines.size() >= kPreviewLines;
            SetDialogStatus(!sticky.empty() ? sticky : plan.summary + (cut ? " The preview shows the first ones." : ""));
        }
        catch (const std::exception& e) { SetDialogStatus(!sticky.empty() ? sticky : e.what()); }
        ShowLines(std::move(lines));
    }
    void Create()
    {
        const auto plan = Plan();
        Json members = Json::array(), copies = Json::array();
        for (const auto& member : plan.members) members.push_back(member.identity);
        for (const auto& copy : plan.copies)
            copies.push_back({ { "before", copy.before }, { "pivot", copy.pivot }, { "yaw", copy.yaw }, { "turn", copy.turn }, { "after", copy.after } });
        const auto created = Editor::PlacementCopy(members, copies, plan.label);
        previewPaused = true;
        ShowLines({});
        Track();
        sticky = plan.label + ": made " + Count(created.size(), "actor", "actors") + " (" + Count(plan.copies.size(), "copy", "copies")
               + "). One Undo removes them. The originals and copies are selected; change a value to preview more.";
        Logger::log("Placement: " + sticky);
        SetDialogStatus(sticky);
    }
    void Enable()
    {
        const auto mode = CurrentMode();
        const bool line = mode == Mode::Linear, radial = mode == Mode::Radial, path = mode == Mode::Path;
        const bool point = !line && CurrentAnchor() == Anchor::Point;
        for (int id : { kOffsetX, kOffsetY, kOffsetZ, kBoxSize }) EnableWindow(Item(id), line);
        EnableWindow(Item(kAnchor), !line);
        for (int id : { kPointX, kPointY, kPointZ }) EnableWindow(Item(id), point);
        for (int id : { kArc, kRadius, kTurn }) EnableWindow(Item(id), radial);
        EnableWindow(Item(kBySpacing), path);
        EnableWindow(Item(kSpacing), path && Checked(kBySpacing));
        EnableWindow(Item(kCount), !(path && Checked(kBySpacing)));
        SetWindowTextA(GetDlgItem(dialog, 1000), path ? "End point" : "Centre");
    }
    // Box size: the line's offset along its main axis becomes the selection's
    // size that way, so copies sit end to end.
    void BoxSizeOffset()
    {
        const auto selection = Selected();
        if (selection.members.empty()) throw std::runtime_error("Select the actors to copy.");
        const auto size = Place::Sub(Place::Union(Boxes(selection)).hi, Place::Union(Boxes(selection)).lo);
        Vector offset{};
        try { offset = { Number(kOffsetX, "X"), Number(kOffsetY, "Y"), Number(kOffsetZ, "Z") }; } catch (const std::exception&) {}
        int axis = 0;
        for (int i = 1; i < 3; ++i) if (std::abs(offset[i]) > std::abs(offset[axis])) axis = i;
        Vector next{};
        next[axis] = (offset[axis] < 0 ? -1 : 1) * size[axis];
        SetText(kOffsetX, Format(next[0])); SetText(kOffsetY, Format(next[1])); SetText(kOffsetZ, Format(next[2]));
    }

    HWND Control(HWND parent, const char* type, const char* text, DWORD style, int id, int x, int y, int width, int height, DWORD exStyle = 0)
    {
        HWND control = CreateWindowExA(exStyle, type, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, parent,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandle(nullptr), nullptr);
        SendMessageA(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        return control;
    }
    HWND Edit(HWND parent, int id, const char* text, int x, int y, int width)
    {
        return Control(parent, "EDIT", text, WS_TABSTOP | ES_AUTOHSCROLL, id, x, y, width, 21, WS_EX_CLIENTEDGE);
    }
    void Build(HWND window)
    {
        int y = 10;
        Control(window, "BUTTON", "In a &line", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, kModeLinear, 12, y, 100, 20);
        Control(window, "BUTTON", "&Round a centre", BS_AUTORADIOBUTTON, kModeRadial, 116, y, 120, 20);
        Control(window, "BUTTON", "Along a &path", BS_AUTORADIOBUTTON, kModePath, 240, y, 120, 20);
        y += 30;
        Control(window, "STATIC", "Copies", WS_GROUP, -1, 12, y + 3, 90, 18);
        Edit(window, kCount, "4", 104, y, 60);
        y += 30;
        Control(window, "STATIC", "Offset X / Y / Z", 0, -1, 12, y + 3, 90, 18);
        Edit(window, kOffsetX, "256", 104, y, 66); Edit(window, kOffsetY, "0", 174, y, 66); Edit(window, kOffsetZ, "0", 244, y, 66);
        Control(window, "BUTTON", "Box size", BS_PUSHBUTTON | WS_TABSTOP, kBoxSize, 316, y - 1, 70, 23);
        y += 34;
        Control(window, "STATIC", "Centre", 0, 1000, 12, y + 3, 90, 18);
        HWND anchor = Control(window, "COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, kAnchor, 104, y, 206, 120);
        SendMessageA(anchor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Builder brush"));
        SendMessageA(anchor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Last-selected actor (not copied)"));
        SendMessageA(anchor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Point typed below"));
        SendMessageA(anchor, CB_SETCURSEL, 0, 0);
        y += 28;
        Control(window, "STATIC", "Point X / Y / Z", 0, -1, 12, y + 3, 90, 18);
        Edit(window, kPointX, "0", 104, y, 66); Edit(window, kPointY, "0", 174, y, 66); Edit(window, kPointZ, "0", 244, y, 66);
        y += 34;
        Control(window, "STATIC", "Arc (degrees)", 0, -1, 12, y + 3, 90, 18);
        Edit(window, kArc, "360", 104, y, 66);
        Control(window, "STATIC", "Radius", 0, -1, 182, y + 3, 50, 18);
        Edit(window, kRadius, "0", 236, y, 74);
        y += 26;
        Control(window, "STATIC", "Radius 0 keeps the selection's own distance from the centre.", 0, -1, 104, y, 290, 16);
        y += 20;
        Control(window, "BUTTON", "&Turn the copies with the circle", BS_AUTOCHECKBOX | WS_TABSTOP, kTurn, 104, y, 260, 20);
        y += 30;
        Control(window, "BUTTON", "One every", BS_AUTOCHECKBOX | WS_TABSTOP, kBySpacing, 104, y, 80, 20);
        Edit(window, kSpacing, "128", 186, y, 66);
        Control(window, "STATIC", "units (instead of a count)", 0, -1, 258, y + 3, 140, 18);
        y += 32;
        Control(window, "BUTTON", "Pre&view in the viewports", BS_AUTOCHECKBOX | WS_TABSTOP, kPreview, 12, y, 200, 20);
        y += 26;
        Control(window, "STATIC", "", SS_LEFT, kStatus, 12, y, 376, 46);
        y += 52;
        Control(window, "BUTTON", "&Create", BS_DEFPUSHBUTTON | WS_TABSTOP, kCreate, 212, y, 84, 26);
        Control(window, "BUTTON", "Close", BS_PUSHBUTTON | WS_TABSTOP, kClose, 304, y, 84, 26);
        SetChecked(kModeLinear, true);
        SetChecked(kTurn, true);
        SetChecked(kPreview, true);
    }
    void Edited()
    {
        if (updating) return;
        previewPaused = false;
        sticky.clear();
        Enable();
        Preview();
    }
    LRESULT CALLBACK DialogProc(HWND window, UINT message, WPARAM w, LPARAM l)
    {
        try
        {
            switch (message)
            {
            case WM_CREATE:
                dialog = window;
                Build(window);
                Enable();
                SetTimer(window, kDialogTimer, 400, nullptr);
                return 0;
            case WM_COMMAND:
            {
                const int id = LOWORD(w), code = HIWORD(w);
                if (id == kClose || id == IDCANCEL) { DestroyWindow(window); return 0; }
                if (id == kCreate || id == IDOK) { Create(); return 0; }
                if (id == kBoxSize) { BoxSizeOffset(); Edited(); return 0; }
                if (code == EN_CHANGE || code == CBN_SELCHANGE || code == BN_CLICKED) { Edited(); return 0; }
                break;
            }
            case WM_TIMER:
                if (w == kDialogTimer)
                {
                    // The selection, the builder brush or the map may have changed.
                    try
                    {
                        const auto generation = Editor::MapGeneration();
                        if (generation != dialogLevelGeneration) { dialogLevelGeneration = generation; previewPaused = false; }
                    }
                    catch (const std::exception&) {}
                    Preview();
                    return 0;
                }
                break;
            case WM_CLOSE:
                DestroyWindow(window);
                return 0;
            case WM_DESTROY:
                KillTimer(window, kDialogTimer);
                ShowLines({});
                return 0;
            case WM_NCDESTROY:
                dialog = nullptr;
                break;
            }
        }
        catch (const std::exception& e)
        {
            sticky = e.what();
            SetDialogStatus(sticky);
            MessageBeep(MB_ICONWARNING);
            Logger::log(std::string("Placement: ") + e.what());
            return 0;
        }
        return DefWindowProcA(window, message, w, l);
    }
    void OpenCopies(Mode mode)
    {
        if (!dialog)
        {
            static bool registered = false;
            if (!registered)
            {
                WNDCLASSA wc{};
                wc.lpfnWndProc = DialogProc; wc.hInstance = GetModuleHandle(nullptr);
                wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
                wc.lpszClassName = "ReloadedPlacementCopies";
                registered = RegisterClassA(&wc) != 0;
            }
            RECT size{ 0, 0, 400, 438 };
            AdjustWindowRectEx(&size, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_TOOLWINDOW);
            HWND owner = GetActiveWindow();
            CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, "ReloadedPlacementCopies", "Placement Copies", WS_CAPTION | WS_SYSMENU | WS_POPUP,
                            CW_USEDEFAULT, CW_USEDEFAULT, size.right - size.left, size.bottom - size.top, owner, nullptr, GetModuleHandle(nullptr), nullptr);
            if (!dialog) throw std::runtime_error("Cannot open the Placement Copies window.");
            RECT r{}; GetWindowRect(dialog, &r);
            POINT at{}; GetCursorPos(&at);
            SetWindowPos(dialog, nullptr, std::max(0L, at.x - 120), std::max(0L, at.y - 60), 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
        SetChecked(kModeLinear, mode == Mode::Linear);
        SetChecked(kModeRadial, mode == Mode::Radial);
        SetChecked(kModePath, mode == Mode::Path);
        previewPaused = false;
        sticky.clear();
        Enable();
        ShowWindow(dialog, SW_SHOW);
        SetForegroundWindow(dialog);
        Preview();
    }

    // ---------------------------------------------------------------- menus

    HMENU BuildMenu()
    {
        HMENU menu = CreatePopupMenu(), align = CreatePopupMenu(), distribute = CreatePopupMenu();
        if (!menu || !align || !distribute) return menu;
        AppendMenuA(menu, MF_STRING, kDropFloor, "&Drop to Floor\tEnd");
        AppendMenuA(menu, MF_STRING, kDropFloorAlign, "Drop to Floor and &Align to Surface\tShift+End");
        AppendMenuA(menu, MF_STRING, kDropCeiling, "Drop to &Ceiling\tCtrl+End");
        AppendMenuA(menu, MF_STRING, kDropWall, "Push to the &Wall Ahead");
        AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
        static const char* labels[9] = { "&X minimum", "X c&entre", "X ma&ximum", "&Y minimum", "Y ce&ntre", "Y maxi&mum", "&Z minimum", "Z cen&tre", "Z maximu&m" };
        for (UINT i = 0; i < 9; ++i)
        {
            if (i == 3 || i == 6) AppendMenuA(align, MF_SEPARATOR, 0, nullptr);
            AppendMenuA(align, MF_STRING, kAlignFirst + i, labels[i]);
        }
        AppendMenuA(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(align), "A&lign to Last Selected");
        AppendMenuA(distribute, MF_STRING, kDistributeX, "Along &X");
        AppendMenuA(distribute, MF_STRING, kDistributeY, "Along &Y");
        AppendMenuA(distribute, MF_STRING, kDistributeZ, "Along &Z");
        AppendMenuA(distribute, MF_STRING, kDistributeLine, "&Between First and Last Selected");
        AppendMenuA(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(distribute), "Distribute &Evenly");
        AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuA(menu, MF_STRING, kCopiesLinear, "Copies in a &Line...");
        AppendMenuA(menu, MF_STRING, kCopiesRadial, "Copies &Round a Centre...");
        AppendMenuA(menu, MF_STRING, kCopiesPath, "Copies Along a &Path...");
        return menu;
    }
    HMENU barMenu = nullptr;
}

void AppendActorMenu(HMENU menu)
{
    if (!menu) return;
    Track();
    if (HMENU placement = BuildMenu()) AppendMenuA(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(placement), "RE+: &Placement");
}

void InstallMenu(HMENU bar)
{
    if (!bar) return;
    HMENU tools = nullptr;
    for (int i = 0; i < GetMenuItemCount(bar) && !tools; ++i)
    {
        char label[64] = {};
        GetMenuStringA(bar, i, label, sizeof(label), MF_BYPOSITION);
        if (!strcmp(label, "RE+ &Tools")) tools = GetSubMenu(bar, i);
    }
    if (!tools) return;
    for (int i = 0; i < GetMenuItemCount(tools); ++i) if (barMenu && GetSubMenu(tools, i) == barMenu) return;
    barMenu = BuildMenu();
    if (!barMenu) return;
    // After the viewport-wide tools (Map Design, Brush Visibility, Storeys).
    int position = 0;
    while (position < GetMenuItemCount(tools) && !(GetMenuState(tools, position, MF_BYPOSITION) & MF_SEPARATOR)) ++position;
    InsertMenuA(tools, position, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(barMenu), "&Placement");
}

bool HandleCommand(UINT command)
{
    if (command < kDropFloor || command > kCopiesPath) return false;
    try
    {
        if (command == kDropFloor) Drop(Place::Surface::Floor, false);
        else if (command == kDropFloorAlign) Drop(Place::Surface::Floor, true);
        else if (command == kDropCeiling) Drop(Place::Surface::Ceiling, false);
        else if (command == kDropWall) Drop(Place::Surface::Wall, false);
        else if (command >= kAlignFirst && command <= kAlignLast)
        {
            const UINT i = command - kAlignFirst;
            Align(static_cast<int>(i / 3), i % 3 == 0 ? Place::Edge::Min : i % 3 == 1 ? Place::Edge::Centre : Place::Edge::Max);
        }
        else if (command == kDistributeX) Distribute(0);
        else if (command == kDistributeY) Distribute(1);
        else if (command == kDistributeZ) Distribute(2);
        else if (command == kDistributeLine) Distribute(-1);
        else if (command == kCopiesLinear) OpenCopies(Mode::Linear);
        else if (command == kCopiesRadial) OpenCopies(Mode::Radial);
        else if (command == kCopiesPath) OpenCopies(Mode::Path);
    }
    catch (const std::exception& e) { Report(e.what(), true); }
    return true;
}

bool ViewportMessage(void* viewport, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_LBUTTONUP || message == WM_RBUTTONDOWN || message == WM_KEYUP
        || (message == WM_MOUSEMOVE && GetTickCount() - lastTrack > 150))
    {
        lastTrack = GetTickCount();
        Track();
        return false;
    }
    if (message != WM_KEYDOWN || wParam != VK_END || (lParam & (1 << 30)) || GetKeyState(VK_MENU) < 0) return false;
    // Only the level viewports: End in a browser's preview is not about the map.
    void* configs = nullptr; int count = 0;
    __try { configs = *reinterpret_cast<void**>(0x1165e8d4); count = *reinterpret_cast<int*>(0x1165e8d8); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (!GridSizeShortcut::Detail::IsLevelViewport(viewport, configs, count)) return false;
    const bool shift = GetKeyState(VK_SHIFT) < 0, control = GetKeyState(VK_CONTROL) < 0;
    if (shift && control) return false;
    HandleCommand(control ? kDropCeiling : shift ? kDropFloorAlign : kDropFloor);
    return true;
}
}
