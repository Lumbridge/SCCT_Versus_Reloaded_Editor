#pragma once
// Find Usages across every map: reading map files from disk. See
// MapUsagesModel.h for the package-table side.
#include "MapUsagesModel.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace MapUsages
{
    // The package bytes of a map file, raw or chunk-compressed as the editor
    // saves .sdc maps. Reads with sharing, so a map the editor has open can
    // still be read; a file locked against reading throws.
    Bytes ReadPackage(const std::filesystem::path& file);

    struct Folder { std::filesystem::path path; std::string label; };
    // The maps a scan of these folders reads, in name order.
    std::vector<std::pair<std::filesystem::path, std::string>> MapFiles(const std::vector<Folder>& folders, bool autosaves);
    // Scans the maps one by one. progress(index, total, file) is called before
    // each; cancel is polled between files. Returns rows for the maps that use
    // the object and for the files that could not be read, sorted.
    std::vector<Row> Scan(const std::vector<std::pair<std::filesystem::path, std::string>>& files,
                          const std::vector<std::string>& query, const std::atomic<bool>& cancel,
                          const std::function<void(size_t, size_t, const std::string&)>& progress);
}
