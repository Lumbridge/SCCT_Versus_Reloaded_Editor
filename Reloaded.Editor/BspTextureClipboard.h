#pragma once

namespace BspTextureClipboard
{
    constexpr UINT kCopyTextureCommandId = 40912;
    constexpr UINT kPasteTextureCommandId = 40913;

    void Initialize();
    void CopySelectedTexture();
    void PasteTexture();
}
