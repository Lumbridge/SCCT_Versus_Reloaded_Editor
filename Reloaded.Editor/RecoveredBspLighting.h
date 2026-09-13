#pragma once
#include <memory>
#include <string>

namespace RecoveredBspLighting
{
    using AssetPath = bool(*)(void*, char*, size_t, const char*);
    class Snapshot;
    std::shared_ptr<Snapshot> Capture(void* model, AssetPath path, const char* package,
                                      std::string& error);
    // Active only during the synchronous recovery lighting command.
    void Activate(const std::shared_ptr<Snapshot>& snapshot);
    void Deactivate();
    void RestoreAtlas(void* pixels, const void* model, const void* texture) noexcept;
    bool Result(const std::shared_ptr<Snapshot>& snapshot, std::string& error);
    bool CheckSavedAtlases(void* model, const std::shared_ptr<Snapshot>& snapshot,
                           bool remember, std::string& error);
}
