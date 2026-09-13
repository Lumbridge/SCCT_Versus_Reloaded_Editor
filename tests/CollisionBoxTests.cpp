#include "../Reloaded.Editor/CollisionBox.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>

int main()
{
    // Endpoints retained in the ClarD lighting crash dump at 0x10EC27F2.
    const uint32_t words[] = {0xC31DA05B, 0xC2AC8914, 0x42A7FE1F,
                             0x3C0AC10D, 0x42F5FAF4, 0x42A8007C};
    float endpoints[6], box[6];
    memcpy(endpoints, words, sizeof(words));
    assert(CollisionBox::FromEndpoints(box, endpoints, endpoints + 3));
    assert(!memcmp(box, endpoints, sizeof(box)));
#if defined(_MSC_VER) && defined(_M_IX86)
    // The decoder may run inside native code with live x87 values. It must
    // neither consume that stack nor need spare x87 registers for its bounds.
    unsigned char savedFpu[108];
    unsigned short status;
    __asm {
        fnsave savedFpu
        fld1
        fld1
        fld1
        fld1
        fld1
        fld1
        fld1
        fld1
    }
    bool fullStackResult = CollisionBox::FromEndpoints(box, endpoints, endpoints + 3);
    __asm { fnstsw status }
    __asm { frstor savedFpu }
    assert(fullStackResult && !(status & 0x41));
    assert(!memcmp(box, endpoints, sizeof(box)));
#endif
    assert(CollisionBox::FromEndpoints(endpoints, endpoints + 3, endpoints));
    assert(!memcmp(box, endpoints, sizeof(box)));
    std::mt19937 random(0xC1A4D);
    for (int trial = 0; trial < 100000; ++trial)
    {
        for (float& value : endpoints)
        {
            uint32_t bits;
            do { bits = static_cast<uint32_t>(random()); }
            while ((bits & 0x7F800000) == 0x7F800000);
            memcpy(&value, &bits, sizeof(value));
        }
        assert(CollisionBox::FromEndpoints(box, endpoints, endpoints + 3));
        for (int axis = 0; axis < 3; ++axis)
        {
            assert(box[axis] <= endpoints[axis] && box[axis] <= endpoints[axis + 3]);
            assert(box[axis + 3] >= endpoints[axis] && box[axis + 3] >= endpoints[axis + 3]);
            assert(box[axis] == endpoints[axis] || box[axis] == endpoints[axis + 3]);
            assert(box[axis + 3] == endpoints[axis] || box[axis + 3] == endpoints[axis + 3]);
        }
    }
    const uint32_t zeroWords[6] = {0x80000000,0x80000000,0x80000000,0,0,0};
    memcpy(endpoints, zeroWords, sizeof(endpoints));
    assert(CollisionBox::FromEndpoints(box, endpoints, endpoints + 3));
    for (const float& value : box) {
        uint32_t bits;
        memcpy(&bits, &value, sizeof(bits));
        assert(bits == 0x80000000); // Equal coordinates retain the first endpoint's bits.
    }
    for (float invalid : {std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        for (int axis = 0; axis < 6; ++axis)
        {
            float input[6] = {}, output[6] = {1,2,3,4,5,6}, before[6];
            memcpy(before, output, sizeof(output));
            input[axis] = invalid;
            assert(!CollisionBox::FromEndpoints(output, input, input + 3));
            assert(!memcmp(before, output, sizeof(output)));
        }
    puts("Collision box tests passed");
}
