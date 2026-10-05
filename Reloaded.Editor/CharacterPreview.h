#pragma once
#include <windows.h>
#include <string>
#include "WorkflowModel.h"

// Live, engine-rendered 3D preview for the Character Skins panel: a spy and a merc
// standing side by side in the materials or models the panel's fields name, holding
// a standing pose from their team's animations. Like the emitter preview
// (EmitterPreview.h), they stand in a private transient level (package
// ReloadedCharacterPreview) that the map never references, drawn by one UViewport
// whose camera lives there; nothing reaches the map, its Undo history or its saved
// package. The figures are removed when the host goes away, so the preview never
// holds the map's own textures past the panel. Drawn on demand (the panel is modal,
// so the editor does not tick meanwhile). UI thread only.
namespace CharacterPreview
{
    bool Attach(HWND host,std::string& error);          // show the preview filling host's client area (a new viewport for a new host)
    void Resize();                                      // host WM_SIZE
    void Detach();                                      // host WM_DESTROY, while its children exist: removes the figures and the viewport
    // slots {SpyBody: path, ...} and models {SpyModel: path, ...} as the panel holds them.
    // Replaces the figures; returns {"poses":{"Spy":sequence,...},"warnings":[...]}.
    // Throws std::runtime_error with a user-facing message.
    Workflow::Json Show(const Workflow::Json& slots,const Workflow::Json& models);
    void Clear();
    void Tick();                                        // host timer: draws when the view changed
    // Called from the UWindowsViewport::ViewportWndProc hook (GridSizeShortcut.cpp)
    // for every viewport; true when the message belonged to the preview and was consumed.
    bool ViewportMessage(void* viewport,UINT message,WPARAM wParam,LPARAM lParam);
    void* Viewport();                                   // the preview's UViewport, or null

    // Tests: renders and reads back the current frame (32-bit BMP when bmpPath is
    // non-empty); returns {width,height,nonBlackPixels,meanLuma,checksum}.
    Workflow::Json Capture(const std::string& bmpPath);
    Workflow::Json State();
    Workflow::Json Camera();
    void SetCamera(const Workflow::Json& camera);
    // A top-level "Character Preview Test" window with one host panel, for the native harness.
    HWND OpenTestWindow(std::string& error);
    void CloseTestWindow();
}
