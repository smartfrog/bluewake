#include "boot_host.h"
#include "pad_input.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct SyntheticSource {
    WwPadSample sample;
    int result;
    unsigned reads;
} SyntheticSource;

static int synthetic_read(void* user, WwPadSample* sample) {
    SyntheticSource* source = user;
    ++source->reads;
    *sample = source->sample;
    return source->result;
}

static u32 read_si(WwHost* host, u32 off) {
    return (u32)host->cpu.external_read(&host->cpu, DOL_SI_BASE + off, 4u);
}

static void write_si(WwHost* host, u32 off, u32 value) {
    host->cpu.external_write(&host->cpu, DOL_SI_BASE + off, value, 4u);
}

static void transfer(WwHost* host, u32 command, u32 length, u32 channel) {
    write_si(host, DOL_SI_IO_BUFFER_OFF, command);
    write_si(host, DOL_SI_COMCSR_OFF, 0x48010001u | (length << 8u) | (channel << 1u));
}

static void test_mapping(void) {
    const struct {
        WwPadSample sample;
        u16 buttons;
        s8 x, y, cx, cy;
    } cases[] = {
        {{0u, 128u, 128u, 128u, 128u}, 0u, 0, 0, 0, 0},
        {{0u, 0u, 0u, 255u, 255u}, 0u, -128, 127, 127, -127},
        {{0u, 255u, 255u, 0u, 0u}, 0u, 127, -127, -128, 127},
        {{0u, 129u, 127u, 127u, 129u}, 0u, 1, 1, -1, -1},
        {{0x8u, 128u, 128u, 128u, 128u}, 0x1000u, 0, 0, 0, 0},
        {{0x4000u, 128u, 128u, 128u, 128u}, 0x0100u, 0, 0, 0, 0},
        {{0x2000u, 128u, 128u, 128u, 128u}, 0x0200u, 0, 0, 0, 0},
        {{0xFFFF9FF7u, 128u, 128u, 128u, 128u}, 0u, 0, 0, 0, 0},
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const DolPadState pad = ww_pad_map(&cases[i].sample);
        assert(pad.button == cases[i].buttons);
        assert(pad.stick_x == cases[i].x && pad.stick_y == cases[i].y);
        assert(pad.substick_x == cases[i].cx && pad.substick_y == cases[i].cy);
        assert(pad.trigger_left == 0u && pad.trigger_right == 0u);
        assert(pad.analog_a == 0u && pad.analog_b == 0u);
    }
}

static void test_live(WwHost* host, SyntheticSource* source) {
    transfer(host, 0x40000300u, 8u, 0u);
    assert(host->error == NULL && source->reads == 1u);
    assert(read_si(host, DOL_SI_IO_BUFFER_OFF) == 0x1300FF01u);
    assert(read_si(host, DOL_SI_IO_BUFFER_OFF + 4u) == 0x007F0000u);
    assert(read_si(host, DOL_SI_STATUS_OFF) == 0x20000000u);
    assert(read_si(host, DOL_SI_COMCSR_OFF) == 0xD8010800u);
    assert(host->interrupts.pi_cause == (DOL_PI_CAUSE_RST_BUTTON | DOL_PI_CAUSE_SI));
}

static void test_id(WwHost* host, SyntheticSource* source, u32 command) {
    transfer(host, command, 3u, 0u);
    assert(host->error == NULL && source->reads == 0u);
    assert(read_si(host, DOL_SI_IO_BUFFER_OFF) == 0x09000000u);
    assert(read_si(host, DOL_SI_IO_BUFFER_OFF + 4u) == 0u);
}

static void test_origin(WwHost* host, SyntheticSource* source, u32 command) {
    write_si(host, DOL_SI_IO_BUFFER_OFF + 8u, 0xAABBCCDDu);
    transfer(host, command, 10u, 0u);
    assert(host->error == NULL && source->reads == 0u);
    assert(read_si(host, DOL_SI_IO_BUFFER_OFF) == 0x09008080u);
    assert(read_si(host, DOL_SI_IO_BUFFER_OFF + 4u) == 0x80800000u);
    assert(read_si(host, DOL_SI_IO_BUFFER_OFF + 8u) == 0x0000CCDDu);
}

static void test_empty_port(WwHost* host, SyntheticSource* source, u32 channel) {
    transfer(host, 0x40000300u, 8u, channel);
    assert(host->error == NULL && source->reads == 0u);
    assert(read_si(host, DOL_SI_IO_BUFFER_OFF) == 0x40000300u);
    assert(read_si(host, DOL_SI_STATUS_OFF) == (0x08000000u >> (channel * 8u)));
    assert(read_si(host, DOL_SI_COMCSR_OFF) == (0xE8010800u | (channel << 1u)));
}

static void test_consume(WwHost* host, SyntheticSource* source, u32 input_offset) {
    transfer(host, 0x40000300u, 8u, 0u);
    assert(dol_si_interrupt_pending(&host->si));
    assert(read_si(host, input_offset) == (input_offset == 4u ? 0x1300FF01u : 0x007F0000u));
    assert(read_si(host, DOL_SI_STATUS_OFF) == 0u);
    assert(read_si(host, DOL_SI_COMCSR_OFF) == 0xC8010800u);
    write_si(host, DOL_SI_COMCSR_OFF, 0xC8010800u);
    assert(!dol_si_interrupt_pending(&host->si));
    assert(host->interrupts.pi_cause == DOL_PI_CAUSE_RST_BUTTON);
    assert(source->reads == 1u);
}

static void test_poll(WwHost* host, SyntheticSource* source, u32 enables) {
    write_si(host, DOL_SI_COMCSR_OFF, DOL_SI_RDSTINTMSK);
    write_si(host, DOL_SI_POLL_OFF, enables);
    assert(host->error == NULL);
    const u32 mask = bluewake_pad_wire_poll_mask(enables);
    assert(source->reads == (mask & 1u));
    u32 status = 0u;
    for (u32 channel = 0u; channel < 4u; ++channel) {
        if (mask & (1u << channel))
            status |= (channel == 0u ? 0x20000000u : 0x08000000u) >> (channel * 8u);
    }
    assert(read_si(host, DOL_SI_STATUS_OFF) == status);
    assert(read_si(host, DOL_SI_COMCSR_OFF) == ((mask & 1u) ? 0x18000000u : 0x08000000u));
    assert(host->interrupts.pi_cause == (DOL_PI_CAUSE_RST_BUTTON | ((mask & 1u) ? DOL_PI_CAUSE_SI : 0u)));
    if (mask & 1u) {
        assert(read_si(host, 4u) == 0x1300FF01u);
        assert(read_si(host, 8u) == 0x007F0000u);
        assert(host->interrupts.pi_cause == DOL_PI_CAUSE_RST_BUTTON);
    }
}

static void test_release(WwHost* host, SyntheticSource* source, bool poll) {
    transfer(host, 0x40000300u, 8u, 0u);
    source->sample = (WwPadSample){0u, 128u, 128u, 128u, 128u};
    if (poll)
        write_si(host, DOL_SI_POLL_OFF, 0x80u);
    else
        transfer(host, 0x40000300u, 8u, 0u);
    assert(host->error == NULL && source->reads == 2u);
    assert(read_si(host, 4u) == 0x00008080u);
    assert(read_si(host, 8u) == 0x80800000u);
}

static void test_absent(WwHost* host, SyntheticSource* source, bool poll) {
    transfer(host, 0x40000300u, 8u, 0u);
    source->result = 0;
    if (poll)
        write_si(host, DOL_SI_POLL_OFF, 0x80u);
    else
        transfer(host, 0x40000300u, 8u, 0u);
    assert(host->error == NULL && source->reads == 2u);
    assert(read_si(host, DOL_SI_STATUS_OFF) == 0x08000000u);
    assert(read_si(host, 4u) == 0u && read_si(host, 8u) == 0u);
    assert((read_si(host, DOL_SI_COMCSR_OFF) & DOL_SI_RDSTINT) == 0u);
}

static void test_refused(WwHost* host, SyntheticSource* source, bool poll, bool unknown, u32 length) {
    DolSiDevice expected = host->si;
    const u32 command = unknown ? 0x54000000u : 0x40000300u;
    if (!poll)
        write_be32(expected.regs + DOL_SI_IO_BUFFER_OFF, command);
    if (poll)
        write_si(host, DOL_SI_POLL_OFF, 0x80u);
    else
        transfer(host, command, length, 0u);
    assert(host->error != NULL);
    assert(host->error_address == DOL_SI_BASE + (poll ? DOL_SI_POLL_OFF : DOL_SI_COMCSR_OFF));
    assert(host->error_pc == host->cpu.pc && host->error_width == 4u);
    assert(host->error_value == (poll ? 0x80u : (0x48010001u | (length << 8u))));
    assert(memcmp(&host->si, &expected, sizeof(expected)) == 0);
    assert(source->reads == (unknown ? 0u : 1u));
    assert(!dol_si_interrupt_pending(&host->si));
    assert(host->interrupts.pi_cause == DOL_PI_CAUSE_RST_BUTTON);
    assert(read_si(host, DOL_SI_COMCSR_OFF) == 0u);
    assert(read_si(host, DOL_SI_STATUS_OFF) == 0u);
    if (source->result == -42)
        assert(strstr(host->error, "FFFFFFD6") != NULL);
}

static void test_legacy_poll(WwHost* host, SyntheticSource* source) {
    host->pad_read = NULL;
    write_si(host, 4u, 0xDEADBEEFu);
    write_si(host, DOL_SI_COMCSR_OFF, DOL_SI_RDSTINTMSK);
    write_si(host, DOL_SI_POLL_OFF, 0xF0u);
    assert(host->error == NULL && source->reads == 0u);
    assert(read_si(host, DOL_SI_STATUS_OFF) == 0u);
    assert(read_si(host, DOL_SI_COMCSR_OFF) == DOL_SI_RDSTINTMSK);
    assert(read_si(host, 4u) == 0xDEADBEEFu);
    assert(host->interrupts.pi_cause == DOL_PI_CAUSE_RST_BUTTON);
}

static void test_credit(WwHost* host, SyntheticSource* source, bool poll) {
    write_si(host, DOL_SI_IO_BUFFER_OFF, 0x40000300u);
    bluewake_cycle_domain_begin_turn(&host->cycles, &host->cpu);
    bluewake_cycle_domain_prepare_dispatch(&host->cycles, &host->cpu);
    host->cpu.downcount = -17;
    write_si(host, poll ? DOL_SI_POLL_OFF : DOL_SI_COMCSR_OFF, poll ? 0x80u : 0x48010801u);
    assert(host->error == NULL && source->reads == 1u);
    assert(host->cycles.absolute_cycles == 17u && host->cycles.dispatch_cycles == 17u);
    assert(host->cpu.cycle_budget == 239 && host->cpu.downcount == 0);
    assert(bluewake_cycle_domain_end_turn(&host->cycles, &host->cpu) == 0u);
    assert(host->cpu.exception == 0u && host->cpu.pc == 0u);
    assert(host->interrupts.pi_cause == (DOL_PI_CAUSE_RST_BUTTON | (poll ? 0u : DOL_PI_CAUSE_SI)));
}

static void test_reconnect(WwHost* host, SyntheticSource* source) {
    source->result = 0;
    transfer(host, 0x40000300u, 8u, 0u);
    assert(read_si(host, DOL_SI_COMCSR_OFF) == 0xE8010800u);
    source->result = 1;
    transfer(host, 0x40000300u, 8u, 0u);
    assert(host->error == NULL && source->reads == 2u);
    assert(read_si(host, DOL_SI_COMCSR_OFF) == 0xD8010800u);
    assert(read_si(host, DOL_SI_STATUS_OFF) == 0x20000000u);
    assert(read_si(host, 4u) == 0x1300FF01u);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    if (strcmp(argv[1], "mapping") == 0) {
        test_mapping();
        return 0;
    }
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    SyntheticSource source = {{0x6008u, 255u, 255u, 0u, 129u}, 1, 0u};
    assert(host->pad_read == NULL);
    host->pad_read = synthetic_read;
    host->pad_user = &source;
    if (strcmp(argv[1], "live") == 0) test_live(host, &source);
    else if (strcmp(argv[1], "status") == 0) test_id(host, &source, 0x00000000u);
    else if (strcmp(argv[1], "reset") == 0) test_id(host, &source, 0xFF000000u);
    else if (strcmp(argv[1], "origin") == 0) test_origin(host, &source, 0x41000000u);
    else if (strcmp(argv[1], "recalibrate") == 0) test_origin(host, &source, 0x42000000u);
    else if (strncmp(argv[1], "empty-", 6u) == 0) test_empty_port(host, &source, (u32)strtoul(argv[1] + 6u, NULL, 10));
    else if (strcmp(argv[1], "consume-high") == 0) test_consume(host, &source, 4u);
    else if (strcmp(argv[1], "consume-low") == 0) test_consume(host, &source, 8u);
    else if (strncmp(argv[1], "poll-", 5u) == 0) test_poll(host, &source, (u32)strtoul(argv[1] + 5u, NULL, 16));
    else if (strcmp(argv[1], "release") == 0) test_release(host, &source, false);
    else if (strcmp(argv[1], "release-poll") == 0) test_release(host, &source, true);
    else if (strcmp(argv[1], "absent-transfer") == 0) test_absent(host, &source, false);
    else if (strcmp(argv[1], "absent-poll") == 0) test_absent(host, &source, true);
    else if (strcmp(argv[1], "error-transfer") == 0) { source.result = -42; test_refused(host, &source, false, false, 8u); }
    else if (strcmp(argv[1], "error-poll") == 0) { source.result = -42; test_refused(host, &source, true, false, 8u); }
    else if (strcmp(argv[1], "unknown") == 0) test_refused(host, &source, false, true, 8u);
    else if (strcmp(argv[1], "unknown-short") == 0) test_refused(host, &source, false, true, 3u);
    else if (strcmp(argv[1], "invalid-count") == 0) { source.result = 2; test_refused(host, &source, false, false, 8u); }
    else if (strcmp(argv[1], "legacy-poll") == 0) test_legacy_poll(host, &source);
    else if (strcmp(argv[1], "credit-transfer") == 0) test_credit(host, &source, false);
    else if (strcmp(argv[1], "credit-poll") == 0) test_credit(host, &source, true);
    else if (strcmp(argv[1], "reconnect") == 0) test_reconnect(host, &source);
    else assert(false);
    const bool credit = strncmp(argv[1], "credit-", 7u) == 0;
    assert(host->cycles.absolute_cycles == (credit ? 17u : 0u));
    assert(host->cpu.timebase == (credit ? 1u : 0u));
    assert(host->vi_retraces == 0u && host->frame_timing.present.index == 0u);
    ww_host_shutdown(host);
    free(host);
    return 0;
}
