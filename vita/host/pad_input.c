#include "pad_input.h"
#include "boot_host.h"

static s8 invert_y(u8 value) {
    // Vita Y grows down; GC Y grows up. Negated -128 must saturate, not wrap.
    const int centered = 128 - (int)value;
    return (s8)(centered > 127 ? 127 : centered);
}

DolPadState ww_pad_map(const WwPadSample* sample) {
    DolPadState pad = {0};
    if (sample->buttons & 0x0008u)
        pad.button |= 0x1000u;
    if (sample->buttons & 0x4000u)
        pad.button |= 0x0100u;
    if (sample->buttons & 0x2000u)
        pad.button |= 0x0200u;
    pad.stick_x = (s8)((int)sample->lx - 128);
    pad.stick_y = invert_y(sample->ly);
    pad.substick_x = (s8)((int)sample->rx - 128);
    pad.substick_y = invert_y(sample->ry);
    return pad;
}

static const char* sample_pad(WwHost* host, DolPadState* pad, bool* present) {
    WwPadSample sample = {0};
    host->pad_source_result = host->pad_read(host->pad_user, &sample);
    *present = host->pad_source_result == 1;
    if (host->pad_source_result < 0 || host->pad_source_result > 1) {
        if (snprintf(host->pad_error, sizeof(host->pad_error), "controller sample failed: result=%08X",
                     (unsigned)host->pad_source_result) < 0)
            return "controller sample error formatting failed";
        return host->pad_error;
    }
    if (*present)
        *pad = ww_pad_map(&sample);
    return NULL;
}

static void si_pad_response(WwHost* host, u32 channel, u32 data0, u32 data1) {
    const u32 response = 4u + channel * 12u;
    write_be32(host->si.regs + response, data0);
    write_be32(host->si.regs + response + 4u, data1);
    host->si.regs[DOL_SI_STATUS_OFF + channel] =
        (host->si.regs[DOL_SI_STATUS_OFF + channel] & 0xF0u) | 0x20u;
}

static void si_pad_absent(WwHost* host) {
    write_be32(host->si.regs + 4u, 0u);
    write_be32(host->si.regs + 8u, 0u);
    write_be32(host->si.regs + DOL_SI_IO_BUFFER_OFF, 0u);
    write_be32(host->si.regs + DOL_SI_IO_BUFFER_OFF + 4u, 0u);
    host->si.regs[DOL_SI_STATUS_OFF] = (host->si.regs[DOL_SI_STATUS_OFF] & 0xD0u) | 8u;
    dol_si_latch_poll(&host->si, 0u);
}

const char* ww_pad_si_write(WwHost* host, u32 address, u32 value) {
    const bool transfer = address == DOL_SI_BASE + DOL_SI_COMCSR_OFF && (value & DOL_SI_TSTART);
    const bool poll = address == DOL_SI_BASE + DOL_SI_POLL_OFF && host->pad_read != NULL;
    const u32 channel = (value >> 1u) & 3u;
    const u32 mask = poll ? bluewake_pad_wire_poll_mask(value) : 0u;
    const bool connected_transfer = transfer && channel == 0u && host->pad_read != NULL;
    BluewakePadReply reply = BLUEWAKE_PAD_REPLY_UNSUPPORTED;
    DolPadState pad = {0};
    bool present = true;
    if (connected_transfer) {
        u32 command = read_be32(host->si.regs + DOL_SI_IO_BUFFER_OFF);
        if (((value >> 16u) & 0x7Fu) == 1u)
            command &= 0xFF000000u;
        const u32 raw_input_length = (value >> 8u) & 0x7Fu;
        const u32 input_length = raw_input_length != 0u ? raw_input_length : 128u;
        reply = bluewake_pad_wire_reply_kind(command, input_length);
        if (reply == BLUEWAKE_PAD_REPLY_UNSUPPORTED)
            return "unsupported controller SI command or input length";
    }
    if ((connected_transfer && reply == BLUEWAKE_PAD_REPLY_LIVE) || (poll && (mask & 1u))) {
        const char* error = sample_pad(host, &pad, &present);
        if (error != NULL)
            return error;
    }
    dol_si_mmio_write(&host->si, address, 4u, value);
    if (connected_transfer && present) {
        u32 data0, data1;
        if (!bluewake_pad_wire_reply(reply, &pad, pad.button, &data0, &data1))
            return "controller SI reply encoding failed";
        si_pad_response(host, 0u, data0, data1);
        write_be32(host->si.regs + DOL_SI_IO_BUFFER_OFF, data0);
        write_be32(host->si.regs + DOL_SI_IO_BUFFER_OFF + 4u, data1);
        if (reply == BLUEWAKE_PAD_REPLY_ORIGIN) {
            host->si.regs[DOL_SI_IO_BUFFER_OFF + 8u] = 0u;
            host->si.regs[DOL_SI_IO_BUFFER_OFF + 9u] = 0u;
        }
        host->si.regs[DOL_SI_COMCSR_OFF] &= ~0x20u;
        dol_si_latch_poll(&host->si, 1u);
    } else if (transfer) {
        if (connected_transfer)
            si_pad_absent(host);
        u8* status = host->si.regs + DOL_SI_STATUS_OFF;
        u8* control = host->si.regs + DOL_SI_COMCSR_OFF;
        // Legacy empty ports leave the receive buffer untouched.
        write_be32(status, read_be32(status) | (0x08000000u >> (channel * 8u)));
        write_be32(control, read_be32(control) | 0x20000000u);
    } else if (poll) {
        for (u32 port = 1u; port < 4u; ++port) {
            if (mask & (1u << port))
                host->si.regs[DOL_SI_STATUS_OFF + port] |= 8u;
        }
        if (mask & 1u) {
            if (present) {
                u32 data0, data1;
                bluewake_pad_wire_encode(&pad, pad.button, &data0, &data1);
                si_pad_response(host, 0u, data0, data1);
                dol_si_latch_poll(&host->si, 1u);
            } else {
                si_pad_absent(host);
            }
        }
    }
    return NULL;
}
