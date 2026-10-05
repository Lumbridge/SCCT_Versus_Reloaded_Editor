#pragma once
#include <windows.h>
#include "MapRecoveryModel.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// File > Recover Compiled Map's windows: the progress window shown while it
// runs and the report shown when it finishes (MapRecoveryModel.h).
namespace MapRecoveryUi
{
    // The progress window runs on its own thread so that it keeps painting,
    // counting elapsed time and taking a Cancel click while the editor's own
    // geometry and lighting builds hold the UI thread. The recovery reads the
    // request at its next checkpoint, so Cancel takes effect between steps.
    class Progress
    {
    public:
        explicit Progress(HWND owner);
        ~Progress();
        Progress(const Progress&) = delete;
        Progress& operator=(const Progress&) = delete;

        // Called from the recovery (UI thread). False once Cancel was
        // pressed while the stage still allowed it.
        bool Update(MapRecoveryModel::Stage stage, double fraction);
        bool Cancelled() const { return cancelled_; }

    private:
        static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
        void Run();
        void Refresh();

        RECT ownerRect_{};
        std::thread thread_;
        HANDLE ready_ = nullptr;
        std::atomic<HWND> window_{nullptr};
        std::atomic<bool> cancelled_{false};
        mutable std::mutex mutex_;
        std::string stage_;
        int percent_ = 0;
        bool cancelAllowed_ = true;
        DWORD started_ = 0;
    };

    struct Report
    {
        std::string summary;
        std::string details;  // Recovery.txt
        std::string folder;   // the recovery folder
        std::vector<MapRecoveryModel::ReportRow> rows;
    };

    // Modeless; one at a time. Clicking a row selects its item in the map
    // and frames it in the viewports.
    void ShowReport(HWND owner, Report report);
}
