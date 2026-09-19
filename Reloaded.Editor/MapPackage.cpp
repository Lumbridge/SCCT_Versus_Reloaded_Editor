#include "MapPackage.h"
#include "Include/nlohmann/json.hpp"
#include "RecoveredAssetPackage.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <zlib.h>

namespace MapPackage
{
namespace
{
namespace fs = std::filesystem;
std::string Fold(std::string s)
{
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z')
            c += 32;
    return s;
}
void Require(bool ok, const std::string &message)
{
    if (!ok)
        throw std::runtime_error(message);
}
struct Scratch
{
    fs::path directory;
    explicit Scratch(const fs::path &parent)
    {
        static std::atomic<unsigned> serial{};
        for (int i = 0; i < 100; ++i)
        {
            auto candidate =
                parent / ("map-package-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                          "-" + std::to_string(serial++));
            if (fs::create_directory(candidate))
            {
                directory = candidate;
                return;
            }
        }
        throw std::runtime_error("Could not reserve a package staging directory.");
    }
    ~Scratch()
    {
        std::error_code ignored;
        fs::remove_all(directory, ignored);
    }
};
struct Reader
{
    std::ifstream file;
    std::uint64_t size, pos = 0;
    explicit Reader(const fs::path &path) : file(path, std::ios::binary), size(fs::file_size(path))
    {
        Require(bool(file), "Cannot read package.");
    }
    void Seek(std::uint64_t offset)
    {
        Require(offset <= size, "Package table offset is outside the file.");
        file.seekg(static_cast<std::streamoff>(offset));
        pos = offset;
    }
    unsigned Byte()
    {
        char c = 0;
        Require(pos < size && bool(file.get(c)), "Truncated package table.");
        ++pos;
        return static_cast<unsigned char>(c);
    }
    std::uint32_t U32()
    {
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i)
            value |= Byte() << (8 * i);
        return value;
    }
    int Compact()
    {
        auto b = Byte();
        bool negative = (b & 128) != 0;
        std::uint32_t value = b & 63;
        bool more = (b & 64) != 0;
        unsigned shift = 6;
        for (int i = 1; more; ++i)
        {
            Require(i < 5, "Invalid compact package index.");
            b = Byte();
            Require(i < 4 || b <= 15, "Compact package index overflow.");
            value |= (b & 127) << shift;
            shift += 7;
            more = (b & 128) != 0;
        }
        Require(value <= 0x7fffffff, "Package index exceeds supported range.");
        return negative ? -static_cast<int>(value) : static_cast<int>(value);
    }
};
std::vector<std::string> RawImports(const fs::path &path)
{
    Reader r(path);
    Require(r.U32() == 0x9e2a83c1, "Invalid Unreal package magic.");
    auto version = r.U32();
    Require((version >= 171 && version <= 175) || version == 300,
            "Unsupported SCCT package version: " + std::to_string(version));
    r.U32();
    auto count = r.U32(), offset = r.U32();
    r.U32();
    r.U32();
    auto imports = r.U32(), importOffset = r.U32();
    Require(count > 0 && count <= 1000000 && imports <= 1000000 && offset >= 36 && importOffset >= 36,
            "Invalid package table counts or offsets.");
    std::vector<std::string> names;
    names.reserve(count);
    r.Seek(offset);
    for (unsigned i = 0; i < count; ++i)
    {
        int length = r.Compact();
        Require(length > 0 && length <= 1024, "Invalid SCCT name length.");
        std::string name;
        for (int j = 0; j < length; ++j)
        {
            auto position = r.pos;
            auto c = static_cast<char>(r.Byte() ^ (version >= 175 ? (position & 255) : 0));
            Require(j == length - 1 ? c == 0 : c != 0, "Invalid SCCT name terminator.");
            if (j < length - 1)
                name += c;
        }
        r.U32();
        names.push_back(std::move(name));
    }
    r.Seek(importOffset);
    std::set<std::string> dependencies;
    auto name = [&]() {
        int index = r.Compact();
        r.Compact(); // SCCT serializes the name index and hash as compact integers.
        Require(index >= 0 && static_cast<size_t>(index) < names.size(), "Invalid imported name index.");
        return names[index];
    };
    for (unsigned i = 0; i < imports; ++i)
    {
        auto typePackage = name(), type = name();
        auto outer = static_cast<int32_t>(r.U32());
        auto object = name();
        Require(outer <= 0 && outer >= -static_cast<int64_t>(imports), "Invalid imported package owner.");
        if (outer == 0)
        {
            Require(Fold(typePackage) == "core" && Fold(type) == "package", "Unsupported root import type.");
            Require(!object.empty() && object != "." && object != ".." &&
                        object.find_first_of("/\\:\r\n\t\"") == std::string::npos,
                    "Invalid imported package name.");
            dependencies.insert(object);
        }
    }
    return {dependencies.begin(), dependencies.end()};
}
bool Runtime(const std::string &name)
{
    // These SCCT Versus packages are supplied by the native game executable.
    static const std::set<std::string> names = {"core", "engine", "sbase", "sgameplayobjects", "softbody"};
    return names.count(Fold(name)) != 0;
}
// Reference image names inside a Map Design workspace file. A workspace that
// cannot be read simply contributes no images; it is never a packaging error.
std::vector<std::string> WorkspaceReferences(const fs::path &workspace)
{
    std::vector<std::string> files;
    try
    {
        std::ifstream input(workspace, std::ios::binary);
        if (!input)
            return files;
        auto document = nlohmann::json::parse(input, nullptr, false);
        if (!document.is_object() || !document.contains("design"))
            return files;
        const auto &design = document.at("design");
        if (!design.is_object() || !design.contains("references") || !design.at("references").is_array())
            return files;
        for (const auto &reference : design.at("references"))
        {
            if (!reference.is_object() || !reference.contains("file") || !reference.at("file").is_string())
                continue;
            auto file = reference.at("file").get<std::string>();
            if (file.empty() || file.size() > 255 || file.find_first_of("\\/:\r\n") != std::string::npos ||
                file.find("..") != std::string::npos)
                continue;
            if (std::find(files.begin(), files.end(), file) == files.end())
                files.push_back(file);
        }
    }
    catch (const std::exception &)
    {
        files.clear();
    }
    return files;
}
void Check(const Entry &entry)
{
    Require(fs::is_regular_file(entry.source) && fs::file_size(entry.source) == entry.size &&
                fs::last_write_time(entry.source) == entry.modified,
            "File changed since the preview; inspect the map again: " + entry.source.filename().string());
}
void U16(std::ostream &out, std::uint16_t n)
{
    for (int i = 0; i < 2; ++i)
        out.put(static_cast<char>(n >> (8 * i)));
}
void U32(std::ostream &out, std::uint32_t n)
{
    for (int i = 0; i < 4; ++i)
        out.put(static_cast<char>(n >> (8 * i)));
}
std::uint32_t Position(std::ostream &out)
{
    auto p = out.tellp();
    Require(p >= 0 && static_cast<std::uint64_t>(p) < 0xffffffffULL, "ZIP exceeds the supported 4 GB limit.");
    return static_cast<std::uint32_t>(p);
}
struct ZipEntry
{
    std::string name;
    std::uint32_t offset, crc = 0, compressed = 0, size = 0;
};
struct Deflater
{
    z_stream stream{};
    bool ready = false;
    ~Deflater()
    {
        if (ready)
            deflateEnd(&stream);
    }
};
ZipEntry ZipFile(std::ostream &out, const std::string &name, std::istream &input)
{
    Require(name.size() < 65536, "ZIP path is too long.");
    ZipEntry entry{name, Position(out)};
    U32(out, 0x04034b50);
    U16(out, 20);
    U16(out, 0x808); // UTF-8 names and a trailing data descriptor.
    U16(out, 8);
    U16(out, 0);
    U16(out, 33);
    U32(out, 0);
    U32(out, 0);
    U32(out, 0);
    U16(out, static_cast<uint16_t>(name.size()));
    U16(out, 0);
    out << name;
    auto start = Position(out);
    Deflater z;
    Require(deflateInit2(&z.stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) == Z_OK,
            "Cannot initialize ZIP compression.");
    z.ready = true;
    std::array<unsigned char, 65536> source{}, buffer{};
    int status = Z_OK;
    do
    {
        input.read(reinterpret_cast<char *>(source.data()), source.size());
        auto count = static_cast<unsigned>(input.gcount());
        Require(!input.bad() && (input.eof() || !input.fail()), "Could not read ZIP input.");
        Require(entry.size < 0xffffffffULL - count, "ZIP input exceeds 4 GB.");
        entry.size += count;
        entry.crc = crc32(entry.crc, source.data(), count);
        z.stream.next_in = source.data();
        z.stream.avail_in = count;
        do
        {
            z.stream.next_out = buffer.data();
            z.stream.avail_out = static_cast<unsigned>(buffer.size());
            status = deflate(&z.stream, input.eof() ? Z_FINISH : Z_NO_FLUSH);
            Require(status == Z_OK || status == Z_STREAM_END, "ZIP compression failed.");
            out.write(reinterpret_cast<char *>(buffer.data()), buffer.size() - z.stream.avail_out);
            Require(bool(out), "Could not write ZIP (check free disk space).");
        } while (z.stream.avail_in || z.stream.avail_out == 0 || (input.eof() && status != Z_STREAM_END));
    } while (status != Z_STREAM_END);
    entry.compressed = Position(out) - start;
    U32(out, 0x08074b50);
    U32(out, entry.crc);
    U32(out, entry.compressed);
    U32(out, entry.size);
    return entry;
}
} // namespace
std::vector<std::string> Imports(const std::filesystem::path &package)
{
    Reader header(package);
    if (header.U32() == 0x9e2a83c1)
        return RawImports(package);
    Scratch scratch(std::filesystem::temp_directory_path());
    auto decoded = scratch.directory / "decoded.package";
    std::string error;
    Require(RecoveredAssetPackage::Write(package, decoded, error), error);
    return RawImports(decoded);
}
Plan Inspect(const std::filesystem::path &gameRoot, const std::filesystem::path &playableMap)
{
    Plan plan;
    auto root = fs::canonical(gameRoot), map = fs::canonical(playableMap);
    Require(Fold(map.extension().string()) == ".sdc" && fs::equivalent(map.parent_path(), root / "Packages" / "Maps"),
            "Choose a saved playable .sdc from this game's Packages/Maps folder.");
    std::map<std::string, std::vector<fs::path>> candidates;
    const std::set<std::string> extensions = {".usx", ".utx", ".utc", ".uax", ".ukx", ".u"};
    for (const auto &directory : {root / "Packages" / "StaticMeshes", root / "Packages" / "Textures",
                                  root / "Packages" / "Sounds", root / "Packages" / "Animations", root / "System"})
        if (fs::exists(directory))
            for (const auto &file : fs::directory_iterator(directory))
                if (file.is_regular_file() && extensions.count(Fold(file.path().extension().string())))
                    candidates[Fold(file.path().stem().string())].push_back(file.path());
    std::map<std::string, size_t> visited;
    std::set<std::string> runtime;
    auto add = [&](const fs::path &path, const std::string &by) {
        auto key = Fold(path.string());
        auto found = visited.find(key);
        if (found != visited.end())
        {
            auto &users = plan.files[found->second].requiredBy;
            if (std::find(users.begin(), users.end(), by) == users.end())
                users.push_back(by);
            return;
        }
        Require(plan.files.size() < 4096, "Dependency limit exceeded (4096 packages).");
        visited[key] = plan.files.size();
        auto utf8 = path.lexically_relative(root).generic_u8string();
        plan.files.push_back({path,
                              std::string(utf8.begin(), utf8.end()),
                              {by},
                              fs::file_size(path),
                              fs::last_write_time(path)});
    };
    add(map, "Playable map");
    // The map-selection image package is addressed by convention, not an import.
    auto image = candidates.find(Fold(map.stem().string()) + "-i");
    if (image != candidates.end())
        for (const auto &path : image->second)
            if (Fold(path.extension().string()) == ".utx" || Fold(path.extension().string()) == ".utc")
                add(path, "Map-selection image");
    // The Map Design workspace keeps reference images, annotations, layers and
    // parametric blockout history with the map. It is optional: a recipient
    // without it still opens an ordinary map.
    auto editor = root / "System" / "ReloadedEditor";
    auto workspace = editor / "Workspaces" / (map.stem().string() + ".json");
    std::set<size_t> workspaceFiles;
    if (fs::is_regular_file(workspace))
    {
        add(workspace, "Map Design workspace");
        workspaceFiles.insert(visited.at(Fold(workspace.string())));
        for (const auto &file : WorkspaceReferences(workspace))
        {
            auto reference = editor / "References" / file;
            if (!fs::is_regular_file(reference))
                continue;
            add(reference, "Map Design reference image");
            workspaceFiles.insert(visited.at(Fold(reference.string())));
        }
    }
    for (size_t i = 0; i < plan.files.size(); ++i)
    {
        const auto file = plan.files[i];
        // Workspace files and reference images carry no package imports.
        if (workspaceFiles.count(i))
            continue;
        try
        {
            for (const auto &name : Imports(file.source))
            {
                if (Fold(name) == Fold(map.stem().string()))
                    continue;
                if (Runtime(name))
                {
                    runtime.insert(name);
                    continue;
                }
                auto found = candidates.find(Fold(name));
                if (found == candidates.end())
                {
                    plan.errors.push_back(file.destination + ": missing package " + name);
                    continue;
                }
                // .utc is the game's cooked texture counterpart to .utx. Ship both
                // when present so runtime/editor package lookup cannot pick a stale copy.
                auto matches = found->second;
                bool texturePair = matches.size() == 2 && ((Fold(matches[0].extension().string()) == ".utx" &&
                                                            Fold(matches[1].extension().string()) == ".utc") ||
                                                           (Fold(matches[1].extension().string()) == ".utx" &&
                                                            Fold(matches[0].extension().string()) == ".utc"));
                if (matches.size() > 1 && !texturePair)
                {
                    plan.errors.push_back(file.destination + ": ambiguous package " + name);
                    continue;
                }
                for (const auto &path : matches)
                    add(path, file.destination);
            }
            Check(file);
        }
        catch (const std::exception &e)
        {
            plan.errors.push_back(file.destination + ": " + e.what());
        }
    }
    plan.runtimePackages.assign(runtime.begin(), runtime.end());
    return plan;
}
std::string Report(const Plan &plan)
{
    std::ostringstream out;
    out << "SCCT Versus map package\r\n\r\nExtract into the game folder containing System and Packages.\r\nBack up "
           "existing files before replacing them.\r\nThis archive contains saved files; unsaved editor changes are not "
           "included.\r\n\r\n";
    for (const auto &file : plan.files)
    {
        out << (file.include ? "INCLUDED: " : "REQUIRED, SUPPLY SEPARATELY: ") << file.destination << " (" << file.size
            << " bytes)\r\n";
        for (const auto &by : file.requiredBy)
            out << "  Required by: " << by << "\r\n";
    }
    out << "\r\nNative packages supplied by the installed game:\r\n";
    for (const auto &name : plan.runtimePackages)
        out << "  " << name << "\r\n";
    out << "\r\nThe dependency scan follows serialized package imports. Assets loaded only by a script's string path "
           "may require additional files.\r\n";
    for (const auto &error : plan.errors)
        out << "ERROR: " << error << "\r\n";
    return out.str();
}
std::vector<bool> MatchBaseFiles(const Plan &plan, const std::filesystem::path &baseRoot)
{
    Require(fs::is_directory(baseRoot / "System") && fs::is_directory(baseRoot / "Packages"),
            "Select the base game folder containing System and Packages.");
    std::vector<bool> matches(plan.files.size(), false);
    for (size_t i = 1; i < plan.files.size(); ++i)
    {
        const auto &file = plan.files[i];
        auto base = baseRoot / std::u8string(file.destination.begin(), file.destination.end());
        if (!fs::is_regular_file(base) || fs::file_size(base) != file.size)
            continue;
        Require(!fs::equivalent(base, file.source),
                "Choose a separate base installation, not the same asset files being packaged.");
        Check(file);
        auto modified = fs::last_write_time(base);
        std::ifstream a(file.source, std::ios::binary), b(base, std::ios::binary);
        Require(bool(a) && bool(b), "Cannot compare base package files.");
        std::array<char, 65536> left{}, right{};
        bool same = true;
        do
        {
            a.read(left.data(), left.size());
            b.read(right.data(), right.size());
            Require(!a.bad() && !b.bad(), "Reading base package files failed.");
            if (a.gcount() != b.gcount() || !std::equal(left.begin(), left.begin() + static_cast<ptrdiff_t>(a.gcount()), right.begin()))
            {
                same = false;
                break;
            }
        } while (a && b);
        Check(file);
        Require(fs::last_write_time(base) == modified && fs::file_size(base) == file.size,
                "Base package changed during comparison.");
        matches[i] = same;
    }
    return matches;
}
void Write(const Plan &plan, const std::filesystem::path &destination)
{
    Require(plan.errors.empty(), "Resolve all dependency errors before packaging.");
    Require(!plan.files.empty() && plan.files.front().include, "The playable map must be included.");
    auto target = fs::absolute(destination);
    Require(!fs::exists(target), "The ZIP already exists. Choose a new filename.");
    std::set<std::string> names{"mappackage-readme.txt"};
    for (const auto &file : plan.files)
    {
        Check(file);
        auto path = fs::path(std::u8string(file.destination.begin(), file.destination.end()));
        Require(!path.is_absolute() && !path.has_root_name() && !file.destination.empty() &&
                    file.destination.find_first_of("\\:\r\n") == std::string::npos,
                "Invalid archive path.");
        for (const auto &part : path)
            Require(part != ".." && part != ".", "Invalid archive path component.");
        Require(names.insert(Fold(file.destination)).second, "Duplicate archive path.");
    }
    Scratch scratch(target.parent_path());
    auto temporary = scratch.directory / "map.zip";
    std::ofstream out(temporary, std::ios::binary);
    Require(bool(out), "Cannot create ZIP.");
    std::vector<ZipEntry> entries;
    for (const auto &file : plan.files)
        if (file.include)
        {
            std::ifstream input(file.source, std::ios::binary);
            Require(bool(input), "Cannot open " + file.destination);
            entries.push_back(ZipFile(out, file.destination, input));
            Check(file);
        }
    std::istringstream report(Report(plan));
    entries.push_back(ZipFile(out, "MapPackage-README.txt", report));
    Require(entries.size() < 65536, "Too many ZIP entries.");
    auto start = Position(out);
    for (const auto &e : entries)
    {
        U32(out, 0x02014b50);
        U16(out, 20);
        U16(out, 20);
        U16(out, 0x808);
        U16(out, 8);
        U16(out, 0);
        U16(out, 33);
        U32(out, e.crc);
        U32(out, e.compressed);
        U32(out, e.size);
        U16(out, static_cast<uint16_t>(e.name.size()));
        U16(out, 0);
        U16(out, 0);
        U16(out, 0);
        U16(out, 0);
        U32(out, 0);
        U32(out, e.offset);
        out << e.name;
    }
    auto size = Position(out) - start;
    U32(out, 0x06054b50);
    U16(out, 0);
    U16(out, 0);
    U16(out, static_cast<uint16_t>(entries.size()));
    U16(out, static_cast<uint16_t>(entries.size()));
    U32(out, size);
    U32(out, start);
    U16(out, 0);
    out.close();
    Require(bool(out), "Finishing ZIP failed.");
    for (const auto &file : plan.files)
        Check(file);
    fs::create_hard_link(temporary, target);
}
} // namespace MapPackage
