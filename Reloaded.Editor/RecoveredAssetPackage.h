#pragma once

#include <filesystem>
#include <string>

namespace RecoveredAssetPackage
{
    // Preserve the logical Unreal package bytes of a chunk-compressed SDC as
    // an external asset package. Raw Unreal-package input is also accepted.
    // The output must not exist. Chunks are decoded with bounded buffers and
    // checked for exact sizes, complete zlib streams and valid package magic.
    //
    // A sibling temporary directory is exclusively reserved, and the complete
    // file is atomically published with a no-replace hard link. Filesystems
    // without hard-link support fail safely. Only our own temporary file and
    // directory are removed. No existing destination or temporary is changed.
    bool Write(const std::filesystem::path& sourceSdc,
               const std::filesystem::path& destinationUsx,
               std::string& error);
}
