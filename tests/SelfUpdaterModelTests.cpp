#include "../Reloaded.Editor/SelfUpdaterModel.h"
#include <iostream>
#include <source_location>
#include <string>
using namespace Updater;
int checks = 0;
void Check(bool value, const char* message, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(message) + " (line " + std::to_string(where.line()) + ")");
}
template <class F> void Reject(F f, std::source_location where = std::source_location::current())
{
    bool caught = false;
    try { f(); } catch (const std::exception&) { caught = true; }
    ++checks;
    if (!caught) throw std::runtime_error("invalid input accepted at line " + std::to_string(where.line()));
}
Version V(const char* text)
{
    auto v = Version::Parse(text);
    if (!v) throw std::runtime_error(std::string("unparsed version ") + text);
    return *v;
}

// A zip writer independent of the reader under test: stored entries only,
// optionally under a folder, with a comment after the directory.
struct ZipBuilder
{
    Bytes b, directory;
    unsigned count = 0;
    static void U16(Bytes& o, unsigned v) { o.push_back(static_cast<std::uint8_t>(v)); o.push_back(static_cast<std::uint8_t>(v >> 8)); }
    static void U32(Bytes& o, std::uint32_t v) { U16(o, v & 0xFFFF); U16(o, v >> 16); }
    void Add(const std::string& name, const Bytes& data, std::uint16_t flags = 0, std::uint32_t crc = 0xFFFFFFFF)
    {
        if (crc == 0xFFFFFFFF) crc = Crc32(data.data(), data.size());
        const auto local = static_cast<std::uint32_t>(b.size());
        U32(b, 0x04034b50); U16(b, 20); U16(b, flags); U16(b, 0); U32(b, 0); U32(b, crc);
        U32(b, static_cast<std::uint32_t>(data.size())); U32(b, static_cast<std::uint32_t>(data.size()));
        U16(b, static_cast<unsigned>(name.size())); U16(b, 3);
        b.insert(b.end(), name.begin(), name.end());
        b.insert(b.end(), {1, 2, 3}); // a local extra field the central one does not repeat
        b.insert(b.end(), data.begin(), data.end());
        auto& d = directory;
        U32(d, 0x02014b50); U16(d, 20); U16(d, 20); U16(d, flags); U16(d, 0); U32(d, 0); U32(d, crc);
        U32(d, static_cast<std::uint32_t>(data.size())); U32(d, static_cast<std::uint32_t>(data.size()));
        U16(d, static_cast<unsigned>(name.size())); U16(d, 0); U16(d, 0); U16(d, 0); U16(d, 0); U32(d, 0); U32(d, local);
        d.insert(d.end(), name.begin(), name.end());
        ++count;
    }
    Bytes Finish(const std::string& comment = "")
    {
        Bytes out = b;
        const auto offset = static_cast<std::uint32_t>(out.size());
        out.insert(out.end(), directory.begin(), directory.end());
        U32(out, 0x06054b50); U16(out, 0); U16(out, 0); U16(out, count); U16(out, count);
        U32(out, static_cast<std::uint32_t>(directory.size())); U32(out, offset);
        U16(out, static_cast<unsigned>(comment.size()));
        out.insert(out.end(), comment.begin(), comment.end());
        return out;
    }
};

Bytes PeFile(std::uint16_t machine, bool dll)
{
    Bytes f(0x200, 0);
    f[0] = 'M'; f[1] = 'Z'; f[0x3C] = 0x80;
    f[0x80] = 'P'; f[0x81] = 'E';
    f[0x84] = machine & 0xFF; f[0x85] = machine >> 8;
    f[0x80 + 22] = 0x02; f[0x80 + 23] = dll ? 0x20 : 0x01;
    return f;
}

Json Asset(const std::string& name, const std::string& digest = "")
{
    Json a = {{"name", name}, {"browser_download_url", "https://github.com/x/releases/download/t/" + name}, {"size", 2048823}};
    if (!digest.empty()) a["digest"] = digest;
    return a;
}

int main()
{
    try
    {
        // Parsing.
        Check(V("v1.3.0-beta.12").ToString() == "1.3.0-beta.12", "tag prefix dropped");
        Check(V("1.21").ToString() == "1.21.0", "two-part version");
        Check(V("2").ToString() == "2.0.0", "one-part version");
        Check(V("1.2.3+build.5").ToString() == "1.2.3", "build metadata ignored");
        for (const char* bad : {"", "v", "1.", ".1", "1..2", "1.2.3.4", "1.2.x", "1.2.3-", "1.2.3-beta..1", "1.2.3-be_ta", "99999999999.0.0", "latest"})
            Check(!Version::Parse(bad), bad);

        // Ordering, as in the semantic versioning examples.
        const char* ordered[] = {"1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2",
                                 "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0", "1.0.1", "1.2.0", "1.10.0", "1.21.0", "2.0.0"};
        for (size_t i = 0; i + 1 < std::size(ordered); ++i)
        {
            Check(V(ordered[i]) < V(ordered[i + 1]), ordered[i]);
            Check(!(V(ordered[i + 1]) < V(ordered[i])), ordered[i + 1]);
        }
        Check(V("1.3.0-beta.9") < V("1.3.0-beta.12"), "beta numbers compare as numbers");
        Check(V("1.3.0-beta.012") == V("1.3.0-beta.12"), "leading zeros in a number");
        Check(V("1.3.0-beta.99999999999999999999") < V("1.3.0-beta.100000000000000000000"), "huge pre-release numbers");
        Check(V("v1.3.0") == V("1.3.0"), "equal versions");

        // Choosing a release.
        Json releases = Json::array({
            {{"tag_name", "v1.3.0-beta.12"}, {"prerelease", true}, {"name", "Beta 12"}, {"body", "## Fixes\r\n\r\n- **Map recovery:** fixed"},
             {"html_url", "https://github.com/x/releases/tag/v1.3.0-beta.12"},
             {"assets", Json::array({Asset("Reloaded_Editor_v1.3.0-beta.12.zip", "sha256:ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef0123456789")})}},
            {{"tag_name", "v1.3.0-beta.9"}, {"prerelease", true}, {"assets", Json::array({Asset("Reloaded_Editor_v1.3.0-beta.9.zip")})}},
            {{"tag_name", "v1.2.5"}, {"prerelease", false}, {"assets", Json::array({Asset("Reloaded_Editor_1.2.5.zip")})}},
            {{"tag_name", "v9.0.0"}, {"draft", true}, {"assets", Json::array({Asset("Reloaded_Editor.zip")})}},
            {{"tag_name", "v8.0.0"}, {"assets", Json::array()}},
            {{"tag_name", "v7.0.0"}, {"assets", Json::array({Asset("Reloaded_Editor_a.zip"), Asset("Reloaded_Editor_b.zip")})}},
            {{"tag_name", "v6.0.0"}, {"assets", Json::array({Asset("Source.zip"), Asset("Reloaded_Editor.dll")})}},
            {{"tag_name", "nightly"}, {"assets", Json::array({Asset("Reloaded_Editor.zip")})}},
            "not a release",
        });
        auto newest = Newest(releases, true);
        Check(newest && newest->tag == "v1.3.0-beta.12", "newest beta chosen; drafts and archive-less releases passed over");
        Check(newest->prerelease && newest->assetName == "Reloaded_Editor_v1.3.0-beta.12.zip" && newest->assetSize == 2048823, "asset read");
        Check(newest->sha256 == "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789", "digest lower-cased without its prefix");
        Check(newest->page == "https://github.com/x/releases/tag/v1.3.0-beta.12" && newest->name == "Beta 12", "page and name");
        auto stable = Newest(releases, false);
        Check(stable && stable->tag == "v1.2.5" && stable->sha256.empty(), "stable only skips pre-releases");
        Check(!Newest(Json::array(), true), "no releases");
        Reject([] { Newest(Json::object(), true); });
        Json badDigest = Json::array({{{"tag_name", "v1.0.0"}, {"assets", Json::array({Asset("Reloaded_Editor.zip", "sha256:xyz")})}}});
        Check(Newest(badDigest, true)->sha256.empty(), "malformed digest ignored");
        Check(IsReleaseArchive("reloaded_editor.ZIP") && !IsReleaseArchive("Reloaded_Editor.zip.sig") && !IsReleaseArchive(".zip"), "archive names");

        // Release notes.
        Check(PlainNotes("## Fixes\r\n\r\n\r\n- **Map recovery:** fixed\n\n## Install\nExtract it.\n\n")
                  == "Fixes\r\n\r\n- Map recovery: fixed\r\n\r\nInstall\r\nExtract it.", "notes flattened");
        Check(PlainNotes("#hashtag") == "#hashtag", "a hash without a space is not a heading");
        const auto cut = PlainNotes(std::string(10, 'a') + "\xC3\xA9", 11);
        Check(cut == std::string(10, 'a') + "...", "cut does not split UTF-8");
        Check(PlainNotes("").empty(), "empty notes");

        // What's new.
        Json one = {{"tag_name", "v2.1.0"}, {"name", "RE+ 2.1.0"}, {"body", "## Added\n- Roll back"}};
        auto [title, body] = TitleAndNotes(one);
        Check(title == "RE+ 2.1.0" && body == "## Added\n- Roll back", "release title and notes");
        Check(TitleAndNotes(Json{{"tag_name", "v2.1.0"}}).first == "v2.1.0", "title falls back to the tag");
        Reject([] { TitleAndNotes(Json::array()); });
        WhatsNew record{"2.1.0", "RE+ 2.1.0\r\nsecond line", PlainNotes("## Added\n- Roll back\n\nMore.", WhatsNew::kNotesLimit)};
        const auto stored = record.Format();
        auto back = WhatsNew::Parse(stored);
        Check(back && back->version == "2.1.0" && back->title == "RE+ 2.1.0 second line" && !back->shown, "record round trip");
        Check(back->notes == "Added\r\n- Roll back\r\n\r\nMore.", "notes kept with their blank lines");
        Check(back->ShowAtStart("2.1.0") && back->ShowAtStart("v2.1.0"), "shown at the first start of its version");
        Check(!back->ShowAtStart("2.0.0") && !back->ShowAtStart("2.1.0-rc.1") && !back->ShowAtStart("junk"), "not for another version");
        back->shown = true;
        auto again = WhatsNew::Parse(back->Format());
        Check(again && again->shown && !again->ShowAtStart("2.1.0") && again->For("2.1.0"), "shown once, still available on demand");
        Check(WhatsNew::Parse("RE+ What's New\nVersion=2.1.0\nShown=1\n")->notes.empty(), "LF record without notes");
        Check(!WhatsNew::Parse("") && !WhatsNew::Parse("Version=2.1.0\r\n\r\nx") && !WhatsNew::Parse("RE+ What's New\r\nVersion=latest\r\n\r\nx"),
              "damaged records refused");

        // Previous versions.
        Check(ProductVersion("RE+ 2.0.0") && *ProductVersion("RE+ 2.0.0") == V("2.0.0"), "RE+ product version");
        Check(ProductVersion("RE+ 2.1.0-rc.1")->ToString() == "2.1.0-rc.1", "release candidate");
        Check(!ProductVersion("2.0.0.1") && !ProductVersion("2.0.0") && !ProductVersion("") && !ProductVersion("RE+ x"), "1.x builds are unknown");
        Check(SkipAfterRollBack("2.1.0", V("2.0.0")) == "v2.1.0", "rolling back skips the version left");
        Check(SkipAfterRollBack("2.1.0", std::nullopt) == "v2.1.0", "unknown previous treated as older");
        Check(SkipAfterRollBack("2.0.0", V("2.1.0")).empty() && SkipAfterRollBack("2.1.0", V("2.1.0")).empty(), "rolling forward skips nothing");

        // Clean-up at start.
        auto plan = [](Leftovers l) {
            std::string s;
            for (const auto& step : CleanUpPlan(l)) s += std::to_string(static_cast<int>(step.action)) + (step.needsDll ? "d" : "") + " ";
            return s;
        };
        Check(plan({}).empty(), "nothing to do");
        Check(plan({true}) == "0 ", "the old DLL becomes the previous one");
        Check(plan({true, true}) == "0 1d ", "with its launcher");
        Check(plan({true, false, true}) == "0 2d ", "a previous launcher that no longer matches goes");
        Check(plan({true, true, true}) == "0 1d ", "a new previous launcher replaces it");
        Check(plan({false, true, true}) == "3 ", "a lone old launcher goes; the previous pair stays");
        Check(plan({false, false, true}).empty(), "previous version left alone");
        Check(plan({true, false, false, true, true}) == "0 4 5 ", "staged files removed");

        // Zip directory.
        Check(Crc32(reinterpret_cast<const std::uint8_t*>("123456789"), 9) == 0xCBF43926u, "CRC-32 check value");
        const Bytes dll = PeFile(0x14C, true), exe = PeFile(0x14C, false);
        ZipBuilder z;
        z.Add("Reloaded_Editor.exe", exe);
        z.Add("Reloaded.Editor.dll", dll);
        z.Add("README.md", {'h', 'i'});
        const auto zip = z.Finish("a comment");
        const auto entries = ReadZip(zip);
        Check(entries.size() == 3, "three entries");
        const auto* e = FindEntry(entries, "reloaded.editor.DLL");
        Check(e && e->method == 0 && e->size == dll.size() && e->compressedSize == dll.size(), "entry found without case");
        Check(Bytes(zip.begin() + e->dataOffset, zip.begin() + e->dataOffset + e->size) == dll, "data offset skips the local extra field");
        Check(e->crc == Crc32(dll.data(), dll.size()), "entry CRC");
        Check(!FindEntry(entries, "Reloaded.Editor.pdb"), "missing entry");
        ZipBuilder nested;
        nested.Add("Reloaded_Editor_v2/Reloaded.Editor.dll", dll);
        Check(FindEntry(ReadZip(nested.Finish()), "Reloaded.Editor.dll") != nullptr, "entry under a folder");
        ZipBuilder twice;
        twice.Add("Reloaded.Editor.dll", dll);
        twice.Add("x/Reloaded.Editor.dll", dll);
        const auto twiceEntries = ReadZip(twice.Finish());
        Reject([&] { FindEntry(twiceEntries, "Reloaded.Editor.dll"); });
        ZipBuilder encrypted;
        encrypted.Add("Reloaded.Editor.dll", dll, 1);
        const auto encryptedZip = encrypted.Finish();
        Reject([&] { ReadZip(encryptedZip); });
        Reject([] { ReadZip(Bytes(10, 0)); });
        Reject([] { ReadZip(Bytes(100, 0)); });
        auto truncated = zip;
        truncated.erase(truncated.begin() + 40, truncated.begin() + 60);
        Reject([&] { ReadZip(truncated); });
        auto badOffset = zip;
        badOffset[badOffset.size() - 9 - 6] = 0xFF; // directory offset beyond the end record
        Reject([&] { ReadZip(badOffset); });
        auto badLocal = zip;
        badLocal[0] = 'X';
        Reject([&] { ReadZip(badLocal); });

        // Images.
        Check(ImageKind(dll) == Image::Dll && ImageKind(exe) == Image::Exe, "i386 DLL and EXE");
        Check(ImageKind(PeFile(0x8664, true)) == Image::None, "64-bit refused");
        Check(ImageKind(Bytes{'M', 'Z'}) == Image::None && ImageKind(Bytes(0x200, 0)) == Image::None, "not an image");
        auto farHeader = dll;
        farHeader[0x3C] = 0xF0; farHeader[0x3D] = 0x01;
        Check(ImageKind(farHeader) == Image::None, "PE header beyond the file");

        std::cout << "SelfUpdaterModelTests: " << checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "SelfUpdaterModelTests FAILED: " << e.what() << "\n";
        return 1;
    }
}
