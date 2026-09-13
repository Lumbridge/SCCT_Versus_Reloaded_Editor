#pragma once
#include <cstdint>
#include <cstring>

namespace CollisionBox
{
    // Native compressed mesh collision boxes contain six floats, without a
    // validity byte. Copy first so the destination may alias either endpoint.
    inline bool FromEndpoints(float* destination, const float* first, const float* second)
    {
        static_assert(sizeof(float) == sizeof(uint32_t));
        uint32_t a[3], b[3], result[6];
        std::memcpy(a, first, sizeof(a));
        std::memcpy(b, second, sizeof(b));
        // Avoid x87 operations, including CRT finite checks. The native caller
        // can have floating-point registers in use during collision traversal.
        auto order = [](uint32_t bits) {
            if (!(bits & 0x7FFFFFFF)) return uint32_t(0x80000000); // equal signed zeros
            return bits & 0x80000000 ? ~bits : bits ^ 0x80000000;
        };
        for (int axis = 0; axis < 3; ++axis)
        {
            if ((a[axis] & 0x7F800000) == 0x7F800000 || (b[axis] & 0x7F800000) == 0x7F800000) return false;
        }
        for (int axis = 0; axis < 3; ++axis)
        {
            result[axis] = order(b[axis]) < order(a[axis]) ? b[axis] : a[axis];
            result[axis + 3] = order(a[axis]) < order(b[axis]) ? b[axis] : a[axis];
        }
        std::memcpy(destination, result, sizeof(result));
        return true;
    }
}
