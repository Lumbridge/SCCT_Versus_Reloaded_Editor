#include "MapUsages.h"
#include "SdcBlockModel.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <zlib.h>

namespace MapUsages
{
namespace
{
    std::uint32_t Le32(const Bytes& b, size_t at)
    {
        return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
    }
}

Bytes ReadPackage(const std::filesystem::path& file)
{
    std::ifstream input(file, std::ios::binary); // MSVC opens with _SH_DENYNO.
    if (!input) throw std::runtime_error("cannot open the file (locked or missing)");
    Bytes raw((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad()) throw std::runtime_error("reading the file failed");
    if (raw.size() < 12) throw std::runtime_error("the file is too short to be a map");
    if (Le32(raw, 0) == Snapshot::kPackageMagic) return raw;
    // Every block header is checked before anything is allocated: a saved map
    // is usually one block as large as the map, so the bound on a block is
    // the file it has to fit in, not a size of its own.
    std::uint64_t total = 0;
    for (size_t at = 0; at < raw.size();)
    {
        if (raw.size() - at < 8) throw std::runtime_error("truncated block header");
        const auto size = Le32(raw, at), packed = Le32(raw, at + 4);
        at += 8;
        if (SdcBlock::Check(size, packed, raw.size() - at, total) != SdcBlock::Fault::None)
            throw std::runtime_error("not an Unreal package or a compressed map");
        total += size;
        at += packed;
    }
    Bytes package(static_cast<size_t>(total));
    for (size_t at = 0, start = 0; at < raw.size();)
    {
        const auto size = Le32(raw, at), packed = Le32(raw, at + 4);
        at += 8;
        uLongf produced = size;
        if (uncompress(package.data() + start, &produced, raw.data() + at, packed) != Z_OK || produced != size)
            throw std::runtime_error("a compressed block is damaged");
        start += size;
        at += packed;
    }
    if (package.size() < 64 || Le32(package, 0) != Snapshot::kPackageMagic) throw std::runtime_error("the decoded data is not an Unreal package");
    return package;
}

std::vector<std::pair<std::filesystem::path, std::string>> MapFiles(const std::vector<Folder>& folders, bool autosaves)
{
    std::vector<std::pair<std::filesystem::path, std::string>> files;
    for (const auto& folder : folders)
    {
        std::error_code error;
        std::vector<std::filesystem::path> here;
        for (std::filesystem::directory_iterator i(folder.path, error), end; !error && i != end; i.increment(error))
            if (i->is_regular_file(error) && ScanFile(i->path().filename().string(), autosaves)) here.push_back(i->path());
        std::sort(here.begin(), here.end(), [](const auto& a, const auto& b) { return Fold(a.filename().string()) < Fold(b.filename().string()); });
        for (auto& path : here) files.emplace_back(std::move(path), folder.label);
    }
    return files;
}

std::vector<Row> Scan(const std::vector<std::pair<std::filesystem::path, std::string>>& files,
                      const std::vector<std::string>& query, const std::atomic<bool>& cancel,
                      const std::function<void(size_t, size_t, const std::string&)>& progress)
{
    std::vector<Row> rows;
    for (size_t i = 0; i < files.size() && !cancel; ++i)
    {
        const auto name = files[i].first.filename().string();
        if (progress) progress(i, files.size(), name);
        Row row{ name, files[i].second, {}, {}, {} };
        try
        {
            row.path = files[i].first.string();
            row.result = MapUsages::Scan(ReadPackage(files[i].first), query);
            if (!row.result.Found()) continue;
        }
        catch (const std::bad_alloc&) { row.error = "out of memory"; }
        catch (const std::exception& e) { row.error = e.what(); }
        rows.push_back(std::move(row));
    }
    std::sort(rows.begin(), rows.end(), RowOrder);
    return rows;
}
}
