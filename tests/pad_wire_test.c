#include "pad_wire.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>

static void test_reply_kind(void) {
    const struct {
        u32 command;
        u32 length;
        BluewakePadReply expected;
    } cases[] = {
        {0x00000000u, 3u, BLUEWAKE_PAD_REPLY_ID},
        {0xFF000000u, 3u, BLUEWAKE_PAD_REPLY_ID},
        {0x40000300u, 8u, BLUEWAKE_PAD_REPLY_LIVE},
        {0x41000000u, 10u, BLUEWAKE_PAD_REPLY_ORIGIN},
        {0x42000000u, 10u, BLUEWAKE_PAD_REPLY_ORIGIN},
        {0x54000000u, 8u, BLUEWAKE_PAD_REPLY_UNSUPPORTED},
        {0x54000000u, 3u, BLUEWAKE_PAD_REPLY_UNSUPPORTED},
        {0x40000300u, 3u, BLUEWAKE_PAD_REPLY_UNSUPPORTED},
        {0x40000300u, 0u, BLUEWAKE_PAD_REPLY_UNSUPPORTED},
        {0x41AABBCCu, 10u, BLUEWAKE_PAD_REPLY_UNSUPPORTED},
        {0x42AABBCCu, 10u, BLUEWAKE_PAD_REPLY_UNSUPPORTED},
        {0x00000000u, 0u, BLUEWAKE_PAD_REPLY_UNSUPPORTED},
        {0x00000000u, 8u, BLUEWAKE_PAD_REPLY_UNSUPPORTED},
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i)
        assert(bluewake_pad_wire_reply_kind(cases[i].command, cases[i].length) == cases[i].expected);
}

static void test_poll_mask(void) {
    for (u32 bits = 0u; bits < 16u; ++bits) {
        const u32 expected = ((bits >> 3u) & 1u) | (((bits >> 2u) & 1u) << 1u) |
                             (((bits >> 1u) & 1u) << 2u) | ((bits & 1u) << 3u);
        assert(bluewake_pad_wire_poll_mask(0xABCD000Fu | (bits << 4u)) == expected);
    }
}

static void test_origin_ignores_live_state(void) {
    const DolPadState pad = {.button = 0x1300u, .stick_x = 127, .stick_y = -128,
                            .substick_x = -100, .substick_y = 100};
    u32 data0, data1;
    assert(bluewake_pad_wire_reply(BLUEWAKE_PAD_REPLY_ORIGIN, &pad, pad.button, &data0, &data1));
    assert(data0 == 0x09008080u);
    assert(data1 == 0x80800000u);
}

static void test_id_reply(void) {
    u32 data0, data1;
    assert(bluewake_pad_wire_reply(BLUEWAKE_PAD_REPLY_ID, NULL, 0u, &data0, &data1));
    assert(data0 == 0x09000000u);
    assert(data1 == 0u);
}

static void test_desktop_legacy_responses(void) {
    const DolPadState pad = {.button = 0x0100u, .stick_x = 12, .stick_y = -4,
                            .substick_x = -2, .substick_y = 7};
    const u32 lengths[] = {1u, 7u, 8u, 10u, 127u};
    for (u32 opcode = 0u; opcode <= 255u; ++opcode) {
        const u32 command = opcode << 24u;
        for (size_t i = 0u; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
            const bool short_reply = lengths[i] < 8u;
            const bool origin = !short_reply && (opcode == 0x41u || opcode == 0x42u);
            BluewakePadReply reply = bluewake_pad_wire_reply_kind(command, lengths[i]);
            if (reply == BLUEWAKE_PAD_REPLY_UNSUPPORTED)
                reply = short_reply ? BLUEWAKE_PAD_REPLY_ID : BLUEWAKE_PAD_REPLY_LIVE;
            u32 data0, data1;
            assert(bluewake_pad_wire_reply(reply, &pad, pad.button, &data0, &data1));
            assert(data0 == (short_reply ? 0x09000000u : origin ? 0x09008080u : 0x01008C7Cu));
            assert(data1 == (short_reply ? 0u : origin ? 0x80800000u : 0x7E870000u));
        }
    }
}

static void test_invalid_reply_preserves_outputs(void) {
    const BluewakePadReply kinds[] = {BLUEWAKE_PAD_REPLY_UNSUPPORTED, BLUEWAKE_PAD_REPLY_LIVE};
    for (size_t i = 0u; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
        u32 data0 = 0xDEADBEEFu, data1 = 0xAABBCCDDu;
        assert(!bluewake_pad_wire_reply(kinds[i], NULL, 0u, &data0, &data1));
        assert(data0 == 0xDEADBEEFu && data1 == 0xAABBCCDDu);
    }
}

int main(void) {
    test_reply_kind();
    test_poll_mask();
    test_origin_ignores_live_state();
    test_id_reply();
    test_desktop_legacy_responses();
    test_invalid_reply_preserves_outputs();
    DolPadState pad = {0};
    DolPadState live = {0};
    DolPadState merged = {0};
    u32 data0 = 0u;
    u32 data1 = 0u;

    bluewake_pad_wire_encode(&pad, 0u, &data0, &data1);
    assert(data0 == 0x00008080u);
    assert(data1 == 0x80800000u);

    bluewake_pad_wire_encode(&pad, 0x0100u, &data0, &data1);
    assert(data0 == 0x01008080u);
    assert((u16)(data0 >> 16) == 0x0100u);

    pad.stick_x = -12;
    pad.stick_y = 24;
    pad.substick_x = 5;
    pad.substick_y = -7;
    pad.trigger_left = 0x30u;
    pad.trigger_right = 0x40u;
    bluewake_pad_wire_encode(&pad, 0x1234u, &data0, &data1);
    assert(data0 == 0x12347498u);
    // 0x1234 carries the digital R bit (0x0020) and not the L bit (0x0040). A
    // digital R click must reach the guest with full analog travel behind it,
    // because the guest's own pause-menu page switch reads mDoCPd_R_LOCK_BUTTON,
    // which mDoCPd_Read derives from mTriggerRight and not from the button bit.
    // The pad's own 0x40 is therefore overridden to full travel while the left
    // trigger, whose bit is clear, keeps the value the pad reported.
    assert(data1 == 0x857930FFu);
    // The same click with no analog value behind it encodes the same way, and an
    // analog value with no click keeps its own.
    pad.trigger_right = 0u;
    bluewake_pad_wire_encode(&pad, 0x0020u, &data0, &data1);
    assert((data1 & 0xFFu) == 0xFFu);
    bluewake_pad_wire_encode(&pad, 0x0000u, &data0, &data1);
    assert((data1 & 0xFFu) == 0x00u);
    bluewake_pad_wire_encode(&pad, 0x0040u, &data0, &data1);
    assert(((data1 >> 8) & 0xFFu) == 0xFFu);

    pad.button = 0x0100u;
    pad.stick_x = -40;
    pad.stick_y = 10;
    pad.trigger_left = 20u;
    live.button = 0x0200u;
    live.stick_x = 30;
    live.stick_y = 80;
    live.trigger_left = 90u;
    live.trigger_right = 70u;
    bluewake_pad_merge(&pad, &live, &merged);
    assert(merged.button == 0x0300u);
    assert(merged.stick_x == -40);
    assert(merged.stick_y == 80);
    assert(merged.trigger_left == 90u);
    assert(merged.trigger_right == 70u);

    puts("Retail SPEC2 PAD wire encoding contract test passed.");
    return 0;
}
