#pragma once
#include <memory>
#include <string>
#include <unordered_set>

namespace RecoveredBspLighting
{
    using AssetPath = bool(*)(void*, char*, size_t, const char*);
    class Snapshot;
    // A non-null exclusion set requests selective, lossless chart transfer on
    // fixed geometry: excluded faces keep the new bake; other charts are copied
    // through native atlas repacking. Null uses spatial recovery reprojection.
    std::shared_ptr<Snapshot> Capture(void* model, AssetPath path, const char* package,
                                      std::string& error,
                                      const std::unordered_set<int>* excludedSurfaces = nullptr);
    // Active only during synchronous recovery or protected build commands.
    void Activate(const std::shared_ptr<Snapshot>& snapshot);
    void Deactivate();
    void RecordNativeBake(const std::shared_ptr<Snapshot>& snapshot);
    std::shared_ptr<Snapshot> PrepareLocalMatch(void* model, const std::shared_ptr<Snapshot>& snapshot,
        const std::unordered_set<int>& selectedSurfaces, std::string& error);
    std::string LocalMatchReport(const std::shared_ptr<Snapshot>& snapshot);
    void RestoreAtlas(void* pixels, const void* model, const void* texture) noexcept;
    bool Result(const std::shared_ptr<Snapshot>& snapshot, std::string& error, bool allowUnmatched = false);
    bool CheckSavedAtlases(void* model, const std::shared_ptr<Snapshot>& snapshot,
                           bool remember, std::string& error);
}
