#pragma once
#include <windows.h>
#include <string>
#include "WorkflowModel.h"

// Live, engine-rendered preview of an Emitter Library entry. The entry is
// imported into a private transient level (package ReloadedEmitterPreview)
// that the open map never references, drawn by one UViewport whose camera
// lives in that level, and ticked from UEditorEngine::Tick. Nothing is added to
// the map, its Undo history or its saved package. The level lives for the session;
// a viewport lives in one host window only and is deleted through the engine when
// that host goes away, as the stock browsers do. UI thread only.
namespace EmitterPreview
{
    void Initialize();                                  // once at DLL init (hook install); safe before the engine is ready
    bool Attach(HWND host,std::string& error);          // show the live preview filling host's client area (a new viewport for a new host)
    void Resize();                                      // host WM_SIZE
    void Detach();                                      // host WM_DESTROY, while its children exist: deletes the viewport
    // entry = {"actors":[{"text":"Begin Actor ... End Actor","position":[x,y,z],"rotation":[p,y,r],...}],
    //          "preview":{optional starting camera: target, distance or radius, pitch, yaw}}
    // Replaces the current content, runs it for a second and frames what its particles
    // cover. Throws std::runtime_error with a user-facing message.
    void Show(const Workflow::Json& entry);
    void Clear();
    bool Active();                                      // attached and showing an entry
    void Step(double seconds);                          // advance the simulation synchronously (tests; no drawing)
    // Renders and reads back the current frame, writes a 32-bit BMP when bmpPath
    // is non-empty; returns {width,height,nonBlackPixels,meanLuma,checksum,particles}.
    Workflow::Json Capture(const std::string& bmpPath);
    Workflow::Json State();                             // level/viewport/window/particle diagnostics
    // Camera for the current entry: {"target":[x,y,z],"distance":d,"pitch":p,"yaw":y,...}.
    Workflow::Json Camera();
    void SetCamera(const Workflow::Json& camera);
    // Called from the UWindowsViewport::ViewportWndProc hook (GridSizeShortcut.cpp)
    // for every viewport; true when the message belonged to the preview and was consumed.
    bool ViewportMessage(void* viewport,UINT message,WPARAM wParam,LPARAM lParam);

    // Test host for the headless harness and manual checks: a top-level
    // "Emitter Preview Test" window with one child host panel. Refused while the
    // preview is shown in another window; closing it deletes its viewport.
    HWND OpenTestWindow(std::string& error);
    void CloseTestWindow();
}
