#include "RecoveredAssetPackage.h"

#include <zlib.h>

#include <array>
#include <cstdint>
#include <fstream>
#include <system_error>

namespace RecoveredAssetPackage
{
    namespace
    {
        constexpr std::uint32_t kPackageMagic = 0x9E2A83C1u;
        constexpr std::uint64_t kMaxPackageBytes = 0x7FFFFFFFull;
        constexpr std::uint32_t kMaxChunkBytes = 64u*1024u*1024u;
        constexpr std::size_t kBufferBytes = 65536;

        std::uint32_t ReadLittle32(const unsigned char* bytes)
        {
            return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1])<<8)
                | (std::uint32_t(bytes[2])<<16) | (std::uint32_t(bytes[3])<<24);
        }

        struct TemporaryOutput
        {
            std::filesystem::path directory;
            std::filesystem::path file;
            bool owned = false;
            ~TemporaryOutput()
            {
                if (!owned) return;
                std::error_code ignored;
                std::filesystem::remove(file,ignored);
                ignored.clear();
                std::filesystem::remove(directory,ignored);
            }
        };

        struct PackageWriter
        {
            std::ofstream& output;
            std::string& error;
            std::uint64_t total = 0;
            std::array<unsigned char,4> magic{};
            std::size_t magicCount = 0;

            bool Append(const unsigned char* bytes,std::size_t count)
            {
                if (count > kMaxPackageBytes || total > kMaxPackageBytes-count)
                {
                    error = "The recovered asset package exceeds the supported package-size limit.";
                    return false;
                }
                for (std::size_t index=0; index<count && magicCount<magic.size(); ++index)
                    magic[magicCount++]=bytes[index];
                if (magicCount==magic.size() && ReadLittle32(magic.data())!=kPackageMagic)
                {
                    error = "The decoded asset data is not an Unreal package (invalid package magic).";
                    return false;
                }
                output.write(reinterpret_cast<const char*>(bytes),static_cast<std::streamsize>(count));
                if (!output)
                {
                    error = "Could not write the recovered asset package temporary file.";
                    return false;
                }
                total+=count;
                return true;
            }
        };

        struct Inflater
        {
            z_stream stream{};
            bool initialized = false;
            ~Inflater() { if (initialized) inflateEnd(&stream); }
        };

        bool DecodeChunk(std::ifstream& input,std::uint32_t uncompressed,
                         std::uint32_t compressed,PackageWriter& writer)
        {
            if (uncompressed==0 || compressed==0
                || uncompressed>kMaxChunkBytes || compressed>kMaxChunkBytes)
            {
                writer.error = "An SDC block declares an invalid or unsupported size.";
                return false;
            }
            if (writer.total > kMaxPackageBytes-uncompressed)
            {
                writer.error = "The recovered asset package exceeds the supported package-size limit.";
                return false;
            }
            Inflater inflater;
            if (inflateInit(&inflater.stream)!=Z_OK)
            {
                writer.error = "Could not initialize SDC asset decompression.";
                return false;
            }
            inflater.initialized = true;
            std::array<unsigned char,kBufferBytes> source{};
            std::array<unsigned char,kBufferBytes> decoded{};
            std::uint32_t remaining = compressed;
            std::uint64_t produced = 0;
            for (;;)
            {
                if (inflater.stream.avail_in==0 && remaining>0)
                {
                    const auto count = static_cast<std::uint32_t>(
                        remaining<source.size() ? remaining : source.size());
                    input.read(reinterpret_cast<char*>(source.data()),count);
                    if (input.gcount()!=static_cast<std::streamsize>(count))
                    {
                        writer.error = "The SDC asset package ends inside a compressed block.";
                        return false;
                    }
                    remaining-=count;
                    inflater.stream.next_in=source.data();
                    inflater.stream.avail_in=count;
                }
                inflater.stream.next_out=decoded.data();
                inflater.stream.avail_out=static_cast<uInt>(decoded.size());
                const uInt previousInput=inflater.stream.avail_in;
                const int status=inflate(&inflater.stream,Z_NO_FLUSH);
                const std::size_t count=decoded.size()-inflater.stream.avail_out;
                produced+=count;
                if (produced>uncompressed)
                {
                    writer.error = "An SDC block expands beyond its declared uncompressed size.";
                    return false;
                }
                if (count && !writer.Append(decoded.data(),count)) return false;
                if (status==Z_STREAM_END)
                {
                    if (remaining!=0 || inflater.stream.avail_in!=0)
                    {
                        writer.error = "An SDC block contains trailing data after its zlib stream.";
                        return false;
                    }
                    if (produced!=uncompressed)
                    {
                        writer.error = "An SDC block does not match its declared uncompressed size.";
                        return false;
                    }
                    return true;
                }
                if (status!=Z_OK || (count==0 && previousInput==inflater.stream.avail_in))
                {
                    writer.error = "An SDC block has an invalid or truncated zlib stream.";
                    return false;
                }
            }
        }
    }

    bool Write(const std::filesystem::path& sourceSdc,
               const std::filesystem::path& destinationUsx,std::string& error)
    {
        error.clear();
        try
        {
            std::error_code filesystemError;
            const auto destinationStatus=std::filesystem::symlink_status(destinationUsx,filesystemError);
            if (std::filesystem::exists(destinationStatus))
            {
                error = "The recovered asset destination already exists; it will not be overwritten.";
                return false;
            }
            if (filesystemError && filesystemError!=std::errc::no_such_file_or_directory)
            {
                error = "Could not inspect the recovered asset destination: "+filesystemError.message();
                return false;
            }
            std::ifstream input(sourceSdc,std::ios::binary);
            if (!input)
            {
                error = "Could not open the compiled map asset source.";
                return false;
            }
            std::array<unsigned char,8> header{};
            input.read(reinterpret_cast<char*>(header.data()),4);
            if (input.gcount()!=4)
            {
                error = "The compiled map asset source is truncated before its header.";
                return false;
            }
            const bool raw=ReadLittle32(header.data())==kPackageMagic;
            input.clear();
            input.seekg(0);
            if (!input)
            {
                error = "Could not seek the compiled map asset source.";
                return false;
            }

            TemporaryOutput temporary;
            temporary.directory=destinationUsx;
            temporary.directory+=".recovery-tmp";
            temporary.file=temporary.directory/"package.tmp";
            filesystemError.clear();
            if (!std::filesystem::create_directory(temporary.directory,filesystemError))
            {
                error = "The asset package temporary path already exists or cannot be created. "
                        "No existing temporary files were changed.";
                return false;
            }
            temporary.owned=true;
            std::ofstream output(temporary.file,std::ios::binary|std::ios::trunc);
            if (!output)
            {
                error = "Could not create the recovered asset package temporary file.";
                return false;
            }
            PackageWriter writer{output,error};
            if (raw)
            {
                std::array<unsigned char,kBufferBytes> buffer{};
                while (input)
                {
                    input.read(reinterpret_cast<char*>(buffer.data()),buffer.size());
                    const auto count=input.gcount();
                    if (count>0 && !writer.Append(buffer.data(),static_cast<std::size_t>(count)))
                        return false;
                }
                if (!input.eof())
                {
                    error = "Reading the raw asset package failed.";
                    return false;
                }
            }
            else
            {
                for (;;)
                {
                    input.read(reinterpret_cast<char*>(header.data()),header.size());
                    if (input.gcount()==0 && input.eof()) break;
                    if (input.gcount()!=static_cast<std::streamsize>(header.size()))
                    {
                        error = "The SDC asset package has a truncated block header or trailing bytes.";
                        return false;
                    }
                    if (!DecodeChunk(input,ReadLittle32(header.data()),ReadLittle32(header.data()+4),writer))
                        return false;
                }
            }
            if (writer.total<64 || writer.magicCount!=4)
            {
                error = "The decoded asset package is too short to contain its package header.";
                return false;
            }
            output.close();
            if (!output)
            {
                error = "Finishing the recovered asset package temporary file failed.";
                return false;
            }
            filesystemError.clear();
            // Unlike portable rename(), create_hard_link never replaces an
            // existing target, including one created after the initial check.
            // The target becomes visible only after all bytes are complete.
            std::filesystem::create_hard_link(temporary.file,destinationUsx,filesystemError);
            if (filesystemError)
            {
                error = "Could not publish the asset package without overwriting another file: "
                    +filesystemError.message();
                return false;
            }
            return true;
        }
        catch (const std::exception& exception)
        {
            error = "Could not preserve the compiled map assets: "+std::string(exception.what());
            return false;
        }
    }
}
