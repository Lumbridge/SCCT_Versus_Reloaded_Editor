// Link against the same zlib library as the editor. Optional argument is a
// compiled Lobby.sdc; its logical package must be 412719 bytes.
#include "../Reloaded.Editor/RecoveredAssetPackage.h"

#include <zlib.h>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

namespace
{
    using Bytes=std::vector<unsigned char>;
    std::filesystem::path root;
    unsigned serial=0;

    void Put32(Bytes& bytes,std::uint32_t value)
    {
        for (unsigned shift=0;shift<32;shift+=8)
            bytes.push_back(static_cast<unsigned char>(value>>shift));
    }
    void Set32(Bytes& bytes,std::size_t offset,std::uint32_t value)
    {
        for (unsigned shift=0;shift<32;shift+=8)
            bytes[offset+shift/8]=static_cast<unsigned char>(value>>shift);
    }
    Bytes Read(const std::filesystem::path& path)
    {
        std::ifstream input(path,std::ios::binary);
        assert(input);
        return Bytes(std::istreambuf_iterator<char>(input),{});
    }
    void Save(const std::filesystem::path& path,const Bytes& bytes)
    {
        std::ofstream output(path,std::ios::binary);
        assert(output);
        output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
        output.close();
        assert(output);
    }
    Bytes Chunk(const Bytes& raw)
    {
        uLongf size=compressBound(static_cast<uLong>(raw.size()));
        Bytes compressed(size);
        assert(compress2(compressed.data(),&size,raw.data(),static_cast<uLong>(raw.size()),9)==Z_OK);
        compressed.resize(size);
        Bytes result;
        Put32(result,static_cast<std::uint32_t>(raw.size()));
        Put32(result,static_cast<std::uint32_t>(compressed.size()));
        result.insert(result.end(),compressed.begin(),compressed.end());
        return result;
    }
    void CheckGood(const Bytes& source,const Bytes& expected)
    {
        const auto prefix=std::to_string(++serial);
        const auto input=root/(prefix+".sdc"), output=root/(prefix+".usx");
        Save(input,source);
        std::string error;
        if (!RecoveredAssetPackage::Write(input,output,error))
        {
            std::fprintf(stderr,"Unexpected package failure: %s\n",error.c_str());
            assert(false);
        }
        assert(error.empty() && Read(output)==expected);
        auto temporary=output;temporary+=".recovery-tmp";
        assert(!std::filesystem::exists(temporary));
    }
    void CheckBad(const Bytes& source,const char* expected)
    {
        const auto prefix=std::to_string(++serial);
        const auto input=root/(prefix+".sdc"), output=root/(prefix+".usx");
        Save(input,source);
        std::string error;
        assert(!RecoveredAssetPackage::Write(input,output,error));
        assert(!std::filesystem::exists(output));
        auto temporary=output;temporary+=".recovery-tmp";
        assert(!std::filesystem::exists(temporary));
        if (error.find(expected)==std::string::npos)
        {
            std::fprintf(stderr,"Expected '%s', got '%s'\n",expected,error.c_str());
            assert(false);
        }
    }
}

int main(int argc,char** argv)
{
    const auto taskName="SCCTAssetPackageTests-"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    root=std::filesystem::temp_directory_path()/taskName;
    assert(std::filesystem::create_directory(root));
    Bytes raw(300000);
    std::uint32_t random=0x12345678;
    for (unsigned char& value:raw)
    {
        random^=random<<13;random^=random>>17;random^=random<<5;
        value=static_cast<unsigned char>(random);
    }
    Set32(raw,0,0x9e2a83c1u);
    const Bytes compressed=Chunk(raw);
    CheckGood(compressed,raw); // Both input/output span multiple fixed buffers.
    CheckGood(raw,raw);

    // Independent chunk streams concatenate without shifting logical offsets.
    Bytes multi;
    for (std::size_t offset=0;offset<raw.size();)
    {
        const std::size_t size=offset==0 ? 2 : (std::min)(std::size_t(75001),raw.size()-offset);
        const Bytes part(raw.begin()+offset,raw.begin()+offset+size);
        const Bytes block=Chunk(part);
        multi.insert(multi.end(),block.begin(),block.end());
        offset+=size;
    }
    CheckGood(multi,raw);
    CheckBad({},"truncated");
    CheckBad({1,2,3,4,5},"block header");
    Bytes malformed=compressed;
    malformed.pop_back();
    CheckBad(malformed,"inside a compressed block");
    malformed=compressed;malformed.push_back(0x73);
    CheckBad(malformed,"trailing bytes");
    malformed=compressed;
    Set32(malformed,0,static_cast<std::uint32_t>(raw.size()+1));
    CheckBad(malformed,"declared uncompressed size");
    malformed=compressed;
    Set32(malformed,0,static_cast<std::uint32_t>(raw.size()-1));
    CheckBad(malformed,"beyond its declared");
    malformed=compressed;malformed[12]^=0xff;
    CheckBad(malformed,"zlib stream");
    malformed=compressed;malformed.push_back(0);
    Set32(malformed,4,static_cast<std::uint32_t>(malformed.size()-8));
    CheckBad(malformed,"trailing data");
    malformed=compressed;
    Set32(malformed,0,0xffffffffu);
    CheckBad(malformed,"unsupported size");
    malformed=compressed;Set32(malformed,4,0);
    CheckBad(malformed,"invalid");
    malformed=raw;malformed[0]=0;
    CheckBad(Chunk(malformed),"package magic");
    CheckBad({0xc1,0x83,0x2a,0x9e},"too short");

    // Existing destinations and existing temporary paths are never truncated
    // or removed, including a temporary directory left by another run.
    const auto input=root/"sentinel-input.sdc";
    const auto output=root/"sentinel.usx";
    const Bytes sentinel{'s','a','f','e'};
    Save(input,compressed);Save(output,sentinel);
    std::string error;
    assert(!RecoveredAssetPackage::Write(input,output,error));
    assert(Read(output)==sentinel);
    assert(std::filesystem::remove(output));
    auto temporary=output;temporary+=".recovery-tmp";
    Save(temporary,sentinel);
    assert(!RecoveredAssetPackage::Write(input,output,error));
    assert(Read(temporary)==sentinel && !std::filesystem::exists(output));
    assert(std::filesystem::remove(temporary));
    assert(std::filesystem::create_directory(temporary));
    Save(temporary/"package.tmp",sentinel);
    assert(!RecoveredAssetPackage::Write(input,output,error));
    assert(Read(temporary/"package.tmp")==sentinel && !std::filesystem::exists(output));
    assert(std::filesystem::remove(temporary/"package.tmp"));
    assert(std::filesystem::remove(temporary));
    assert(!RecoveredAssetPackage::Write(root/"does-not-exist.sdc",output,error));
    assert(!std::filesystem::exists(output));

    if (argc>1)
    {
        const auto lobby=root/"LobbyAssets.usx";
        if (!RecoveredAssetPackage::Write(argv[1],lobby,error))
        {
            std::fprintf(stderr,"Lobby package failed: %s\n",error.c_str());
            assert(false);
        }
        const Bytes lobbyBytes=Read(lobby);
        assert(lobbyBytes.size()==412719);
        assert(lobbyBytes[0]==0xc1 && lobbyBytes[1]==0x83 && lobbyBytes[2]==0x2a && lobbyBytes[3]==0x9e);
        std::puts("Real Lobby.sdc decompressed: valid UE header, 412719 logical bytes");
    }

    // This directory was exclusively created by this test. No recursive
    // deletion: any unexpected directory makes cleanup fail visibly.
    assert(std::filesystem::equivalent(root.parent_path(),std::filesystem::temp_directory_path()));
    for (const auto& entry:std::filesystem::directory_iterator(root))
        assert(std::filesystem::remove(entry.path()));
    assert(std::filesystem::remove(root));
    std::puts("Recovered asset package tests passed");
}
