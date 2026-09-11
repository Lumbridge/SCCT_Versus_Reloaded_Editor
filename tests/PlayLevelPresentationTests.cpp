// Link with d3d8types.cpp and user32.lib/gdi32.lib. This test never creates a
// window or Direct3D device. Run both without and with -ReloadedEditorPlay.
#include "../Reloaded.Editor/Include/d3d8to9/source/d3d8types.hpp"
#include "../Reloaded.Editor/Include/d3d8to9/source/PlayLevelPresentation.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
    using PlayLevelPresentation::HasMarker;
    assert(HasMarker("game -ReloadedEditorPlay"));
    assert(HasMarker("\"C:\\Game Files\\SCCT Versus\" map.sdc -ReloadedEditorPlay -log"));
    assert(HasMarker("game\t\"-ReloadedEditorPlay\"\t"));
    assert(!HasMarker(nullptr));
    assert(!HasMarker(""));
    assert(!HasMarker("game -ReloadedEditorPlayExtra"));
    assert(!HasMarker("game prefix-ReloadedEditorPlay"));
    assert(!HasMarker("game -ReloadedEditorPlay=1"));
    assert(!HasMarker("game map.sdc?-ReloadedEditorPlay"));
    assert(!HasMarker("game -reloadededitorplay"));
    assert(!HasMarker("game \"echo -ReloadedEditorPlay\""));
    assert(!HasMarker("game -Exec=\"echo -ReloadedEditorPlay\""));
    assert(!HasMarker(R"(game -Exec="echo \" -ReloadedEditorPlay tail")"));
    assert(HasMarker(R"(game -Exec="echo \" -ReloadedEditorPlay tail" -ReloadedEditorPlay)"));

    const bool enabled = argc == 2 && std::strcmp(argv[1], "-ReloadedEditorPlay") == 0;
    assert(PlayLevelPresentation::IsEnabledForProcess() == enabled);
    const D3DSWAPEFFECT effects[] = {
        D3DSWAPEFFECT_DISCARD, D3DSWAPEFFECT_COPY,
        static_cast<D3DSWAPEFFECT>(D3DSWAPEFFECT_COPY_VSYNC)
    };
    const UINT intervals[] = { D3DPRESENT_INTERVAL_THREE, D3DPRESENT_INTERVAL_IMMEDIATE };
    const BOOL modes[] = { FALSE, TRUE };
    for (BOOL windowed : modes)
    {
        for (D3DSWAPEFFECT effect : effects)
        {
            for (UINT interval : intervals)
            {
                D3DPRESENT_PARAMETERS8 input = {};
                input.BackBufferWidth = 1920;
                input.BackBufferHeight = 1080;
                input.BackBufferFormat = D3DFMT_X8R8G8B8;
                input.BackBufferCount = 1;
                input.Windowed = windowed;
                input.FullScreen_RefreshRateInHz = 120;
                input.FullScreen_PresentationInterval = interval;
                input.SwapEffect = effect;
                input.EnableAutoDepthStencil = TRUE;
                input.AutoDepthStencilFormat = D3DFMT_D24S8;
                const D3DPRESENT_PARAMETERS8 original = input;
                D3DPRESENT_PARAMETERS output = {};
                ConvertPresentParameters(input, output);
                assert(std::memcmp(&input, &original, sizeof(input)) == 0);
                assert(output.BackBufferWidth == 1920 && output.BackBufferHeight == 1080);
                assert(output.BackBufferFormat == D3DFMT_X8R8G8B8);
                assert(output.BackBufferCount == 1);
                assert(output.EnableAutoDepthStencil == TRUE);
                assert(output.AutoDepthStencilFormat == D3DFMT_D24S8);
                assert(output.SwapEffect == (effect == D3DSWAPEFFECT_COPY_VSYNC ? D3DSWAPEFFECT_COPY : effect));
                if (enabled)
                {
                    assert(output.Windowed == TRUE);
                    assert(output.FullScreen_RefreshRateInHz == 0);
                    assert(output.PresentationInterval == D3DPRESENT_INTERVAL_IMMEDIATE);
                }
                else
                {
                    // The normal editor/game conversion retains its previous
                    // fullscreen, windowed and COPY_VSYNC behavior.
                    assert(output.Windowed == windowed);
                    assert(output.FullScreen_RefreshRateInHz == (windowed ? 0u : 120u));
                    UINT expectedInterval = windowed ? D3DPRESENT_INTERVAL_IMMEDIATE : interval;
                    if (effect == D3DSWAPEFFECT_COPY_VSYNC && (windowed || interval == D3DPRESENT_INTERVAL_IMMEDIATE))
                        expectedInterval = D3DPRESENT_INTERVAL_ONE;
                    assert(output.PresentationInterval == expectedInterval);
                }
            }
        }
    }
    std::puts(enabled ? "Marked Play Level presentation tests passed"
                      : "Normal presentation tests passed");
}
