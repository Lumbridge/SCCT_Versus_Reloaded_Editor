// Pure placement/state tests: no windows, Direct3D devices or display changes.
#include "../Reloaded.Editor/Include/d3d8to9/source/PlayLevelWindow.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace
{
    RECT CheckPlacement(const RECT& workArea, LONG frameWidth, LONG frameHeight,
        UINT renderWidth, UINT renderHeight)
    {
        RECT result = {};
        assert(PlayLevelWindow::CalculatePlacement(workArea, frameWidth, frameHeight,
            renderWidth, renderHeight, result));

        const LONG outerWidth = result.right - result.left;
        const LONG outerHeight = result.bottom - result.top;
        const LONG clientWidth = outerWidth - frameWidth;
        const LONG clientHeight = outerHeight - frameHeight;
        const LONG workWidth = workArea.right - workArea.left;
        const LONG workHeight = workArea.bottom - workArea.top;
        assert(clientWidth > 0 && clientHeight > 0);
        assert(result.left >= workArea.left && result.right <= workArea.right);
        assert(result.top >= workArea.top && result.bottom <= workArea.bottom);
        assert(static_cast<long long>(outerWidth) * 10 <= static_cast<long long>(workWidth) * 9);
        assert(static_cast<long long>(outerHeight) * 10 <= static_cast<long long>(workHeight) * 9);

        // Integer-pixel placement can differ by one pixel on opposite margins.
        assert(std::llabs(static_cast<long long>(result.left) + result.right
            - workArea.left - workArea.right) <= 1);
        assert(std::llabs(static_cast<long long>(result.top) + result.bottom
            - workArea.top - workArea.bottom) <= 1);

        // Preserve the rendered image's aspect ratio to within pixel rounding.
        const long long aspectError = std::llabs(static_cast<long long>(clientWidth) * renderHeight
            - static_cast<long long>(clientHeight) * renderWidth);
        const UINT roundingAllowance = renderWidth > renderHeight ? renderWidth : renderHeight;
        assert(aspectError <= roundingAllowance);
        return result;
    }

    void CheckInvalid(const RECT& workArea, LONG frameWidth, LONG frameHeight,
        UINT renderWidth = 640, UINT renderHeight = 480)
    {
        RECT result = {};
        assert(!PlayLevelWindow::CalculatePlacement(workArea, frameWidth, frameHeight,
            renderWidth, renderHeight, result));
    }
}

int main()
{
    const RECT desktop = { 0, 0, 1920, 1040 };

    // Regression: a legacy 640x480 render should occupy a useful, centered
    // portion of the desktop instead of retaining its tiny top-left window.
    const RECT enlarged = CheckPlacement(desktop, 16, 39, 640, 480);
    assert(enlarged.right - enlarged.left - 16 > 1000);
    assert(enlarged.bottom - enlarged.top - 39 > 750);
    assert(enlarged.left > 0 && enlarged.top > 0);

    // The fit includes the title bar and borders; even small work areas fit.
    const RECT smallMonitor = { 0, 0, 320, 200 };
    const RECT smallPlacement = CheckPlacement(smallMonitor, 16, 39, 640, 480);
    assert(smallPlacement.right - smallPlacement.left - 16 < 640);
    assert(smallPlacement.bottom - smallPlacement.top - 39 < 480);

    // Secondary displays can lie left of and above the primary display.
    const RECT negativeMonitor = { -2560, -1440, 0, -40 };
    const RECT secondary = CheckPlacement(negativeMonitor, 16, 39, 1920, 1080);
    assert(secondary.right < 0 && secondary.bottom < 0);

    const RECT portraitMonitor = { 1920, 0, 3000, 1880 };
    CheckPlacement(portraitMonitor, 16, 39, 1080, 1920);
    CheckPlacement(portraitMonitor, 16, 39, 1920, 1080);

    // A borderless window uses the same fit policy without inventing a frame.
    const RECT wideMonitor = { 0, 0, 1920, 1080 };
    const RECT borderless = CheckPlacement(wideMonitor, 0, 0, 1920, 1080);
    assert(borderless.right - borderless.left == 1728);
    assert(borderless.bottom - borderless.top == 972);

    // Render dimensions are unsigned; their aspect calculations must not
    // overflow through a 32-bit intermediate.
    const UINT maxRender = (std::numeric_limits<UINT>::max)();
    CheckPlacement(desktop, 16, 39, maxRender, maxRender);
    CheckPlacement(desktop, 16, 39, maxRender, maxRender / 2);

    CheckInvalid(RECT{ 0, 0, 0, 1080 }, 16, 39);
    CheckInvalid(RECT{ 0, 0, 1920, 0 }, 16, 39);
    CheckInvalid(RECT{ 20, 0, 10, 1080 }, 16, 39);
    CheckInvalid(RECT{ 0, 20, 1920, 10 }, 16, 39);
    CheckInvalid(desktop, 16, 39, 0, 480);
    CheckInvalid(desktop, 16, 39, 640, 0);
    CheckInvalid(desktop, 1728, 39); // No client width within the 90% fit.
    CheckInvalid(desktop, 16, 936);  // No client height within the 90% fit.
    CheckInvalid(desktop, 2000, 39);
    CheckInvalid(desktop, 16, 1100);
    CheckInvalid(desktop, -1, 39);
    CheckInvalid(desktop, 16, -1);
    CheckInvalid(RECT{ 0, 0, 1, 1 }, 0, 0);
    CheckInvalid(desktop, 16, 39, 1, maxRender);
    CheckInvalid(desktop, 16, 39, maxRender, 1);
    CheckInvalid(RECT{ (std::numeric_limits<LONG>::min)(), 0,
        (std::numeric_limits<LONG>::max)(), 1040 }, 16, 39);

    PlayLevelWindow::PlacementState state;
    assert(state.IsPending());
    assert(state.Begin());
    assert(!state.IsPending());
    assert(!state.Begin());

    // Sizing can synchronously cause a reset. It must not re-arm sizing while
    // the current placement is applying, or the two operations can loop.
    state.Request();
    assert(!state.IsPending());
    assert(!state.Begin());
    state.Complete();
    assert(!state.IsPending());
    assert(!state.Begin());

    // A later real device reset requests exactly one fresh placement.
    state.Request();
    state.Request();
    assert(state.IsPending());
    assert(state.Begin());
    assert(!state.IsPending());
    state.Complete();
    assert(!state.Begin());
    state.Request();
    assert(state.Begin());
    state.Complete();
    assert(!state.IsPending());

    std::puts("Play Level window placement and lifecycle tests passed");
}
