#pragma once
// Find Usages across every map: the pure part. A map file's package tables
// say which objects of other packages the map uses (its imports); the tagged
// property lists of its own objects say which of them refer to one. No
// engine, no zlib and no files here, so the tests compile it alone; the
// container decoding and the folder walk live in MapUsages.cpp.
#include "LevelSnapshotModel.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace MapUsages
{
    using Bytes = Snapshot::Bytes;
    using Snapshot::Package;

    inline std::string Fold(std::string s)
    {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // "Package.Group.Name", also accepted as copied from the editor with its
    // class, Texture'Package.Group.Name'. Empty parts are refused.
    inline std::vector<std::string> SplitPath(std::string query)
    {
        auto trim = [](std::string& s)
        {
            const auto first = s.find_first_not_of(" \t\r\n"), last = s.find_last_not_of(" \t\r\n");
            s = first == std::string::npos ? std::string{} : s.substr(first, last - first + 1);
        };
        trim(query);
        const auto quote = query.find('\'');
        if (quote != std::string::npos && query.size() > quote + 1 && query.back() == '\'')
            query = query.substr(quote + 1, query.size() - quote - 2);
        trim(query);
        if (query.empty()) throw std::runtime_error("Enter the object's full name, Package.Group.Name.");
        std::vector<std::string> parts;
        size_t start = 0;
        for (;;)
        {
            const auto dot = query.find('.', start);
            auto part = query.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
            // Groups may contain spaces ("Oilrig_SM.Third Floor.oiltanks").
            if (part.empty() || part.find_first_of("'\"\r\n\t") != std::string::npos)
                throw std::runtime_error("'" + query + "' is not an object name like Package.Group.Name.");
            parts.push_back(part);
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        return parts;
    }

    // An object's name chain, outermost first. Index follows the package
    // convention: -1 is the first import, 1 the first export, 0 nothing.
    inline std::vector<std::string> Chain(const Package& p, int index)
    {
        std::vector<std::string> parts;
        for (int depth = 0; index != 0; ++depth)
        {
            if (depth > 64) throw std::runtime_error("Package object owners form a loop.");
            if (index < 0)
            {
                const auto at = static_cast<size_t>(-static_cast<long long>(index) - 1);
                if (at >= p.imports.size()) throw std::runtime_error("Object owner is outside the import table.");
                parts.insert(parts.begin(), p.imports[at].name);
                index = p.imports[at].outer;
            }
            else
            {
                const auto at = static_cast<size_t>(index - 1);
                if (at >= p.exports.size()) throw std::runtime_error("Object owner is outside the export table.");
                parts.insert(parts.begin(), p.exports[at].name);
                index = p.exports[at].outer;
            }
        }
        return parts;
    }
    inline std::string Join(const std::vector<std::string>& parts)
    {
        std::string out;
        for (const auto& part : parts) out += (out.empty() ? "" : ".") + part;
        return out;
    }

    // Imports whose name chain is the query, compared without case. When the
    // query names a package or a group, everything imported from inside it
    // matches as well. Returns import positions (0 = first import).
    inline std::vector<size_t> MatchImports(const Package& p, const std::vector<std::string>& query)
    {
        std::vector<std::string> folded;
        for (const auto& part : query) folded.push_back(Fold(part));
        bool container = false;
        std::vector<size_t> exact, inside;
        for (size_t i = 0; i < p.imports.size(); ++i)
        {
            std::vector<std::string> chain;
            try { chain = Chain(p, -static_cast<int>(i) - 1); }
            catch (const std::exception&) { continue; }
            if (chain.size() < folded.size()) continue;
            bool same = true;
            for (size_t k = 0; k < folded.size() && same; ++k) same = Fold(chain[k]) == folded[k];
            if (!same) continue;
            if (chain.size() == folded.size())
            {
                exact.push_back(i);
                if (Fold(p.imports[i].className) == "package") container = true;
            }
            else inside.push_back(i);
        }
        if (container) exact.insert(exact.end(), inside.begin(), inside.end());
        return exact;
    }

    // Object references in an export's tagged property list. SCCT writes a
    // number after every name reference that the tag sizes do not count, so
    // the reader keeps the "logical" position the sizes are measured in.
    // Structs and arrays are read as tagged structs, object lists or name
    // lists when their contents fit the declared size exactly; anything else
    // is skipped by size. Stops at the list's None: data after it (mesh, BSP,
    // texture mips) is native and not read.
    struct PropertyReader
    {
        const Bytes& b;
        const std::vector<std::string>& names;
        size_t pos, end, hidden = 0;
        int depth = 0;
        struct Failure {};
        size_t Logical() const { return pos - hidden; }
        std::uint8_t U8() { if (pos >= end) throw Failure{}; return b[pos++]; }
        int Compact()
        {
            auto byte = U8();
            const bool negative = (byte & 0x80) != 0;
            std::uint32_t value = byte & 0x3f;
            bool more = (byte & 0x40) != 0;
            for (int shift = 6, i = 1; more; shift += 7, ++i)
            {
                if (i >= 5) throw Failure{};
                byte = U8();
                value |= static_cast<std::uint32_t>(byte & 0x7f) << shift;
                more = (byte & 0x80) != 0;
            }
            if (value > 0x7fffffff) throw Failure{};
            return negative ? -static_cast<int>(value) : static_cast<int>(value);
        }
        const std::string& Name()
        {
            const int index = Compact();
            const auto before = pos;
            Compact(); // The saving process's name number; not counted by tag sizes.
            hidden += pos - before;
            if (index < 0 || static_cast<size_t>(index) >= names.size()) throw Failure{};
            return names[static_cast<size_t>(index)];
        }
        void Skip(size_t count) { if (count > end - pos) throw Failure{}; pos += count; }
        // Whether the bytes ahead start a property tag or the closing None.
        bool NextIsTag()
        {
            const auto savedPos = pos, savedHidden = hidden;
            bool ok = false;
            try
            {
                const int index = Compact();
                Compact();
                if (index >= 0 && static_cast<size_t>(index) < names.size())
                    ok = names[static_cast<size_t>(index)] == "None" || (U8() & 0xf) != 0;
            }
            catch (const Failure&) {}
            pos = savedPos; hidden = savedHidden;
            return ok;
        }
        // Appends (property path, object index) pairs; throws Failure when the
        // list does not parse.
        void Tags(std::vector<std::pair<std::string, int>>& out, const std::string& prefix)
        {
            if (++depth > 8) throw Failure{};
            for (int count = 0; count < 4096; ++count)
            {
                const std::string name = Name();
                if (name == "None") { --depth; return; }
                const auto info = U8();
                const int type = info & 0xf, sizeCode = (info >> 4) & 7;
                if (type == 10) Name(); // Struct name.
                size_t size = 0;
                switch (sizeCode)
                {
                case 0: size = 1; break; case 1: size = 2; break; case 2: size = 4; break; case 3: size = 12; break; case 4: size = 16; break;
                case 5: size = U8(); break;
                case 6: size = U8(); size |= static_cast<size_t>(U8()) << 8; break;
                default: size = U8(); size |= static_cast<size_t>(U8()) << 8; size |= static_cast<size_t>(U8()) << 16; size |= static_cast<size_t>(U8()) << 24; break;
                }
                if (type == 3) continue; // A bool's value is the array bit.
                if (info & 0x80)
                {
                    const auto index = U8();
                    if (index & 0x80) { U8(); if (index & 0x40) { U8(); U8(); } }
                }
                const auto start = Logical();
                const std::string path = prefix + name;
                if (type == 5 || type == 8) out.emplace_back(path, Compact());
                else if (type == 6) Name();
                else if (type == 10 || type == 9) Contents(out, path, type == 9, start, size);
                else Skip(size);
                if (Logical() - start != size) throw Failure{};
            }
            throw Failure{};
        }
        void Contents(std::vector<std::pair<std::string, int>>& out, const std::string& path, bool array, size_t start, size_t size)
        {
            const auto savedPos = pos, savedHidden = hidden;
            const auto savedDepth = depth;
            auto attempt = [&](int form)
            {
                pos = savedPos; hidden = savedHidden; depth = savedDepth;
                std::vector<std::pair<std::string, int>> found;
                try
                {
                    if (!array) Tags(found, path + ".");
                    else
                    {
                        const int count = Compact();
                        if (count < 0 || static_cast<size_t>(count) > size) return false;
                        for (int i = 0; i < count; ++i)
                        {
                            if (form == 0) found.emplace_back(path, Compact());
                            else if (form == 1) Tags(found, path + ".");
                            else Name();
                        }
                    }
                }
                catch (const Failure&) { return false; }
                // A list of names also reads as a list of object indexes of
                // the right size, leaving the names' numbers unread; the next
                // tag is what tells them apart.
                if (Logical() - start != size || !NextIsTag()) return false;
                out.insert(out.end(), found.begin(), found.end());
                return true;
            };
            if (array ? attempt(0) || attempt(1) || attempt(2) : attempt(0)) return;
            pos = savedPos; hidden = savedHidden; depth = savedDepth;
            Skip(size);
        }
    };

    constexpr const char* kBrushFaces = "Brush faces";

    // The references in one export's property list, or false when the list
    // cannot be read (references found before the failure are discarded).
    // A brush's Polys object also lists each face's material after its
    // properties, which is where BSP textures are named in a source map:
    // those come back under kBrushFaces.
    inline bool PropertyReferences(const Bytes& b, const Package& p, const Snapshot::Export& e, std::vector<std::pair<std::string, int>>& out, bool polys = false)
    {
        if (!e.size || e.offset > b.size() || e.size > b.size() - e.offset) return e.size == 0;
        PropertyReader r{ b, p.names, e.offset, static_cast<size_t>(e.offset) + e.size };
        std::vector<std::pair<std::string, int>> found;
        try
        {
            if (e.flags & 0x02000000) // RF_HasStack: the state frame comes first.
            {
                const int node = r.Compact();
                r.Compact();
                r.Skip(12); // Probe mask and latent action.
                if (node != 0) r.Compact();
            }
            r.Tags(found, "");
            if (polys)
            {
                // Count and capacity, then each FPoly: vertex count, base,
                // normal, texture U and V, the vertices, flags, actor,
                // material, item name, link, brush poly, two floats.
                auto int32 = [&] { std::uint32_t v = r.U8(); v |= r.U8() << 8; v |= r.U8() << 16; v |= static_cast<std::uint32_t>(r.U8()) << 24; return static_cast<std::int32_t>(v); };
                const auto count = int32(), capacity = int32();
                if (count < 0 || capacity < count || count > 1000000) throw PropertyReader::Failure{};
                for (int i = 0; i < count; ++i)
                {
                    const int vertices = r.Compact();
                    if (vertices < 0 || vertices > 64) throw PropertyReader::Failure{};
                    r.Skip(48 + 12 * static_cast<size_t>(vertices) + 4);
                    r.Compact();
                    const int material = r.Compact();
                    if (material) found.emplace_back(kBrushFaces, material);
                    r.Name();
                    r.Compact(); r.Compact();
                    r.Skip(8);
                }
                if (r.pos != r.end) throw PropertyReader::Failure{};
            }
        }
        catch (const PropertyReader::Failure&) { return false; }
        out.insert(out.end(), found.begin(), found.end());
        return true;
    }

    struct MapResult
    {
        std::vector<std::string> objects;        // Matched imports other than packages and groups, full names.
        size_t packages = 0;                     // Matched packages and groups.
        std::map<std::string, size_t> users;     // "StaticMeshActor.StaticMesh" -> count.
        size_t references = 0;                   // Sum of users.
        std::vector<std::string> unreferenced;   // Matched but in no property list.
        size_t unparsed = 0;                     // Exports whose properties could not be read.
        bool Found() const { return !objects.empty() || packages; }
    };

    inline std::string ExportClass(const Package& p, const Snapshot::Export& e)
    {
        if (e.classIndex < 0) return e.className;
        if (e.classIndex > 0 && static_cast<size_t>(e.classIndex) <= p.exports.size()) return p.exports[static_cast<size_t>(e.classIndex) - 1].name;
        return "Class";
    }

    // Everything one decoded package says about the query. Property lists
    // are only read when an import matched, so most maps cost a table read.
    inline MapResult Scan(const Bytes& package, const std::vector<std::string>& query)
    {
        const auto p = Snapshot::Parse(package);
        MapResult result;
        const auto matched = MatchImports(p, query);
        if (matched.empty()) return result;
        std::map<int, std::string> wanted;
        for (auto i : matched)
        {
            const int index = -static_cast<int>(i) - 1;
            if (Fold(p.imports[i].className) == "package") { ++result.packages; continue; }
            wanted[index] = Join(Chain(p, index));
            result.objects.push_back(wanted[index]);
        }
        std::set<int> seen;
        for (const auto& e : p.exports)
        {
            std::vector<std::pair<std::string, int>> refs;
            const auto type = ExportClass(p, e);
            if (!PropertyReferences(package, p, e, refs, type == "Polys")) { ++result.unparsed; continue; }
            for (const auto& ref : refs)
                if (wanted.count(ref.second))
                {
                    ++result.users[ref.first == kBrushFaces ? ref.first : type + "." + ref.first];
                    ++result.references;
                    seen.insert(ref.second);
                }
        }
        for (const auto& w : wanted) if (!seen.count(w.first)) result.unreferenced.push_back(w.second);
        return result;
    }

    // "Used by" text for the results list, most frequent first.
    inline std::string Summary(const MapResult& r)
    {
        std::vector<std::pair<size_t, std::string>> order;
        for (const auto& u : r.users) order.emplace_back(u.second, u.first);
        std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
        std::string text;
        for (const auto& o : order) text += (text.empty() ? "" : ", ") + o.second + " x" + std::to_string(o.first);
        if (!r.unreferenced.empty())
        {
            text += text.empty() ? "" : "; ";
            text += r.unreferenced.size() == r.objects.size() && r.objects.size() == 1
                ? "only in native data (built BSP surfaces, a map-local mesh)"
                : std::to_string(r.unreferenced.size()) + " object(s) only in native data (built BSP surfaces, map-local meshes)";
        }
        return text;
    }

    struct Row { std::string file, folder; MapResult result; std::string error, path; };
    inline bool RowOrder(const Row& a, const Row& b)
    {
        if (a.error.empty() != b.error.empty()) return a.error.empty();
        if (a.result.references != b.result.references) return a.result.references > b.result.references;
        if (Fold(a.file) != Fold(b.file)) return Fold(a.file) < Fold(b.file);
        return Fold(a.folder) < Fold(b.folder);
    }

    // Tab-separated text for the clipboard, with a header line.
    inline std::string Report(const std::string& query, const std::vector<Row>& rows, size_t scanned)
    {
        size_t found = 0;
        for (const auto& r : rows) found += r.error.empty();
        std::string text = "Usages of " + query + ": " + std::to_string(found) + " of " + std::to_string(scanned) + " maps\r\n";
        text += "Map\tFolder\tReferences\tObjects\tUsed by\r\n";
        for (const auto& r : rows)
        {
            if (!r.error.empty()) { text += r.file + "\t" + r.folder + "\t-\t-\tCould not read: " + r.error + "\r\n"; continue; }
            text += r.file + "\t" + r.folder + "\t" + std::to_string(r.result.references) + "\t" + std::to_string(r.result.objects.size()) + "\t" + Summary(r.result) + "\r\n";
        }
        return text;
    }

    // Which files a scan reads: .sdc maps, not the editor's Auto0-Auto9
    // autosaves unless asked, nor backup copies with other extensions.
    inline bool ScanFile(const std::string& fileName, bool autosaves)
    {
        const auto name = Fold(fileName);
        if (name.size() < 5 || name.substr(name.size() - 4) != ".sdc") return false;
        const auto stem = name.substr(0, name.size() - 4);
        if (!autosaves && stem.size() == 5 && stem.rfind("auto", 0) == 0 && std::isdigit(static_cast<unsigned char>(stem[4]))) return false;
        return true;
    }
}
