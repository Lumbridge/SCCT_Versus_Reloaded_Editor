#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace MapPackage
{
struct Entry
{
    std::filesystem::path source;
    std::string destination;
    std::vector<std::string> requiredBy;
    std::uintmax_t size{};
    std::filesystem::file_time_type modified{};
    bool include = true;
};
struct Plan
{
    std::vector<Entry> files;
    std::vector<std::string> runtimePackages, errors;
};
// Reads only package tables. Never loads a map into the editor.
std::vector<std::string> Imports(const std::filesystem::path &package);
Plan Inspect(const std::filesystem::path &gameRoot, const std::filesystem::path &playableMap);
std::string Report(const Plan &plan);
// True only for byte-identical files in a user-selected base installation.
std::vector<bool> MatchBaseFiles(const Plan &plan, const std::filesystem::path &baseRoot);
// Writes to a private temporary directory and publishes without replacement.
void Write(const Plan &plan, const std::filesystem::path &destination);
} // namespace MapPackage
