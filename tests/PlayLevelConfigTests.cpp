// File/configuration regressions only: no windows, game, or Direct3D devices.
#include "../Reloaded.Editor/PlayLevelConfig.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>

namespace
{
    struct TemporaryIni
    {
        char path[MAX_PATH] = {};

        TemporaryIni()
        {
            char directory[MAX_PATH] = {};
            const DWORD length = GetTempPathA(MAX_PATH, directory);
            assert(length != 0 && length < MAX_PATH);
            assert(GetTempFileNameA(directory, "plc", 0, path) != 0);
        }

        ~TemporaryIni()
        {
            WritePrivateProfileStringA(nullptr, nullptr, nullptr, path);
            DeleteFileA(path);
        }
    };

    std::string ReadBytes(const char* path)
    {
        std::ifstream file(path, std::ios::binary);
        assert(file.is_open());
        return std::string(std::istreambuf_iterator<char>(file),
                           std::istreambuf_iterator<char>());
    }

    void WriteBytes(const char* path, const std::string& content)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        assert(file.is_open());
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        assert(file.good());
    }

    std::string ReadValue(const char* path, const char* section, const char* key)
    {
        char value[512] = {};
        const DWORD length = GetPrivateProfileStringA(section, key, "<missing>",
            value, static_cast<DWORD>(sizeof(value)), path);
        assert(length < sizeof(value) - 1);
        return std::string(value, length);
    }

    void CheckResolution(PlayLevelConfig::Resolution actual, UINT width, UINT height)
    {
        assert(actual.width == width && actual.height == height);
    }

    void CheckWrittenResolution(const char* path, UINT width, UINT height)
    {
        const char* widthKeys[] = { "FullscreenViewportX", "WindowedViewportX", "MenuViewportX" };
        const char* heightKeys[] = { "FullscreenViewportY", "WindowedViewportY", "MenuViewportY" };
        for (const char* key : widthKeys)
            assert(ReadValue(path, "WinDrv.WindowsClient", key) == std::to_string(width));
        for (const char* key : heightKeys)
            assert(ReadValue(path, "WinDrv.WindowsClient", key) == std::to_string(height));
        assert(ReadValue(path, "WinDrv.WindowsClient", "StartupFullscreen") == "False");
    }
}

int main()
{
    using PlayLevelConfig::SelectResolution;
    const PlayLevelConfig::Resolution display = { 2560, 1440 };
    CheckResolution(SelectResolution(1920, 1080, display), 1920, 1080);
    CheckResolution(SelectResolution(640, 480, display), 640, 480);
    CheckResolution(SelectResolution(0, 0, display), 2560, 1440);
    CheckResolution(SelectResolution(1920, 0, display), 2560, 1440);
    CheckResolution(SelectResolution(0, 1080, display), 2560, 1440);
    CheckResolution(SelectResolution(319, 1080, display), 2560, 1440);
    CheckResolution(SelectResolution(1920, 199, display), 2560, 1440);
    CheckResolution(SelectResolution(16385, 1080, display), 2560, 1440);
    CheckResolution(SelectResolution(1920, 16385, display), 2560, 1440);
    CheckResolution(SelectResolution((std::numeric_limits<UINT>::max)(), 1080, display), 2560, 1440);
    CheckResolution(SelectResolution(0, 0, { 0, 0 }), 1280, 720);
    CheckResolution(SelectResolution(0, 0, { 0, 1080 }), 1280, 720);
    CheckResolution(SelectResolution(0, 0, { 1920, 0 }), 1280, 720);
    CheckResolution(SelectResolution(0, 0, { 200, 100 }), 1280, 720);
    CheckResolution(SelectResolution(0, 0, { 16385, 1080 }), 1280, 720);
    CheckResolution(SelectResolution(1920, 1080, { 0, 0 }), 1920, 1080);

    TemporaryIni source;
    TemporaryIni destination;
    const std::string highBitValue = "Class\xE9(677)'SBase\xF1(180).Example'";
    const std::string sourceBytes =
        "; Preserve the complete configuration.\r\n"
        "[WinDrv.WindowsClient]\r\n"
        "FullscreenViewportX=640\r\nFullscreenViewportY=480\r\n"
        "WindowedViewportX=640\r\nWindowedViewportY=480\r\n"
        "MenuViewportX=640\r\nMenuViewportY=480\r\n"
        "StartupFullscreen=True\r\nBrightness=0.750000\r\n"
        "[Engine.Engine]\r\nRenderDevice=D3DDrv.D3DRenderDevice\r\n"
        "[SBase.SPlayerProfile]\r\nScreenRes=0\r\nObject=" + highBitValue + "\r\n";
    WriteBytes(source.path, sourceBytes);

    assert(PlayLevelConfig::WriteConfiguration(source.path, destination.path, { 1920, 1080 }));
    CheckWrittenResolution(destination.path, 1920, 1080);
    assert(ReadBytes(source.path) == sourceBytes);
    assert(ReadValue(destination.path, "WinDrv.WindowsClient", "Brightness") == "0.750000");
    assert(ReadValue(destination.path, "Engine.Engine", "RenderDevice") == "D3DDrv.D3DRenderDevice");
    assert(ReadValue(destination.path, "SBase.SPlayerProfile", "ScreenRes") == "0");
    assert(ReadValue(destination.path, "SBase.SPlayerProfile", "Object") == highBitValue);
    assert(ReadBytes(destination.path).find(highBitValue) != std::string::npos);

    // Reusing the generated INI must replace every old resolution value.
    assert(PlayLevelConfig::WriteConfiguration(source.path, destination.path, { 1600, 1200 }));
    CheckWrittenResolution(destination.path, 1600, 1200);
    assert(ReadBytes(source.path) == sourceBytes);
    assert(ReadValue(destination.path, "SBase.SPlayerProfile", "Object") == highBitValue);
    assert(ReadBytes(destination.path).find(highBitValue) != std::string::npos);

    // A failed preparation must not silently succeed or overwrite the source.
    const std::string beforeFailure = ReadBytes(destination.path);
    assert(!PlayLevelConfig::WriteConfiguration(source.path, destination.path, { 0, 1080 }));
    assert(GetLastError() == ERROR_INVALID_PARAMETER);
    assert(ReadBytes(destination.path) == beforeFailure);
    const std::string missingSource = std::string(source.path) + ".missing";
    assert(GetFileAttributesA(missingSource.c_str()) == INVALID_FILE_ATTRIBUTES);
    assert(!PlayLevelConfig::WriteConfiguration(missingSource.c_str(), destination.path, { 1920, 1080 }));
    assert(GetLastError() == ERROR_FILE_NOT_FOUND);
    assert(ReadBytes(destination.path) == beforeFailure);
    assert(ReadBytes(source.path) == sourceBytes);

    std::puts("Play Level configuration tests passed (no graphics or game launched).");
    return 0;
}
