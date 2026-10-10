// Link against the same zlib library as the editor. Optional argument is a
// compiled Lobby.sdc; its logical package must be 412719 bytes.
#include "../Reloaded.Editor/RecoveredAssetPackage.h"

#include <zlib.h>

#include <algorithm>
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
    // One block: the sizes, then a zlib stream. Level 0 stores the bytes, so
    // the compressed size is a little over the uncompressed one.
    Bytes Chunk(const Bytes& raw,int level=9)
    {
        uLongf size=compressBound(static_cast<uLong>(raw.size()));
        Bytes result(8+size);
        assert(compress2(result.data()+8,&size,raw.data(),static_cast<uLong>(raw.size()),level)==Z_OK);
        result.resize(8+size);
        Set32(result,0,static_cast<std::uint32_t>(raw.size()));
        Set32(result,4,static_cast<std::uint32_t>(size));
        return result;
    }
    bool Matches(const std::filesystem::path& path,const Bytes& expected)
    {
        std::ifstream input(path,std::ios::binary);
        assert(input);
        Bytes buffer(1u<<20);
        std::size_t offset=0;
        for (;;)
        {
            input.read(reinterpret_cast<char*>(buffer.data()),buffer.size());
            const auto count=static_cast<std::size_t>(input.gcount());
            if (count==0) return offset==expected.size();
            if (count>expected.size()-offset
                || !std::equal(buffer.begin(),buffer.begin()+count,expected.begin()+offset))
                return false;
            offset+=count;
        }
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
        assert(error.empty() && Matches(output,expected));
        auto temporary=output;temporary+=".recovery-tmp";
        assert(!std::filesystem::exists(temporary));
        // Large cases would otherwise fill the temporary folder until the end.
        assert(std::filesystem::remove(input) && std::filesystem::remove(output));
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

    // A map saved by the editor is one block as large as the map. Blocks over
    // the former 64 MB limit decode like any other: one that did not compress
    // (both sizes over 64 MB), and one of zeros, which is zlib's best ratio
    // and so the closest a real stream comes to the size a header may declare.
    {
        Bytes large(72u*1024u*1024u+4321u);
        for (unsigned char& value:large)
        {
            random^=random<<13;random^=random>>17;random^=random<<5;
            value=static_cast<unsigned char>(random);
        }
        Set32(large,0,0x9e2a83c1u);
        {
            const Bytes stored=Chunk(large,0);
            assert(stored.size()>large.size());
            CheckGood(stored,large);
        }

        // The same package as the game writes it: 15 MB blocks and a shorter
        // last one.
        Bytes blocks;
        blocks.reserve(large.size()+4096);
        for (std::size_t offset=0;offset<large.size();)
        {
            const std::size_t size=(std::min)(std::size_t(15u*1024u*1024u),large.size()-offset);
            const Bytes block=Chunk(Bytes(large.begin()+offset,large.begin()+offset+size),0);
            blocks.insert(blocks.end(),block.begin(),block.end());
            offset+=size;
        }
        CheckGood(blocks,large);
    }
    {
        Bytes sparse(96u*1024u*1024u);
        Set32(sparse,0,0x9e2a83c1u);
        const Bytes packed=Chunk(sparse);
        assert(sparse.size()/(packed.size()-8)>900);
        CheckGood(packed,sparse);
    }

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
    // Headers that cannot be right are refused before anything is decoded:
    // more data than the compressed bytes can hold, a compressed size past
    // the end of the file, an empty size, a package over the 1 GB limit.
    malformed=compressed;
    Set32(malformed,0,0xffffffffu);
    CheckBad(malformed,"more data than its compressed bytes");
    malformed=compressed;
    Set32(malformed,0,static_cast<std::uint32_t>((compressed.size()-8)*1032+1));
    CheckBad(malformed,"more data than its compressed bytes");
    malformed=compressed;
    Set32(malformed,4,0xffffffffu);
    CheckBad(malformed,"inside a compressed block");
    malformed=compressed;Set32(malformed,4,0);
    CheckBad(malformed,"invalid");
    malformed=compressed;Set32(malformed,0,0);
    CheckBad(malformed,"invalid");
    malformed.assign(8+1100000,0);
    Set32(malformed,0,1024u*1024u*1024u+1u);
    Set32(malformed,4,1100000);
    CheckBad(malformed,"1 GB");
    malformed=compressed;
    malformed.insert(malformed.end(),8+1100000,0);
    Set32(malformed,compressed.size(),1024u*1024u*1024u-static_cast<std::uint32_t>(raw.size())+1u);
    Set32(malformed,compressed.size()+4,1100000);
    CheckBad(malformed,"1 GB");
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
