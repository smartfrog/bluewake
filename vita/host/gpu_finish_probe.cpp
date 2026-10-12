extern "C" {
#include "probe_support.h"
}
#include "ww_renderer_backend.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <cstdarg>

static bool fail_trace;
static bool fail_frontier_trace;

extern "C" int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (stream == stderr &&
        ((fail_trace && std::strncmp(format, "[gx-gpu-finish]", 15u) == 0) ||
         (fail_frontier_trace && std::strncmp(format, "[gx-finish-frontier]", 20u) == 0))) {
        errno = ENOSPC;
        return -1;
    }
    va_list arguments;
    va_start(arguments, format);
    const int result = std::vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

class RecordingFinishBackend final : public WwRendererBackend {
public:
    const char* init() override { return nullptr; }
    const char* submit_draw(const gx_vita::TranslatedDraw&, const gx_vita::CgProgram&) override {
        return "recording finish backend has no GPU draw support";
    }
    const char* submit_copy(const gx_vita::TranslatedCopy&) override {
        return "recording finish backend has no GPU copy support";
    }
    const char* present() override { return "recording finish backend has no display"; }
    const char* finish() override {
        ++finishes;
        return fail ? "synthetic GPU finish failed" : nullptr;
    }
    void shutdown() override {}
    u32 finishes = 0u;
    bool fail = false;
};

int main(int argc, char** argv) {
    assert(argc == 2);
    const char* scenario = argv[1];
    WwHost* host = static_cast<WwHost*>(std::calloc(1u, sizeof(*host)));
    assert(host != nullptr && ww_host_init(host));
    CPUState* cpu = &host->cpu;
    RecordingFinishBackend backend;
    backend.fail = std::strcmp(scenario, "fence-failure") == 0;
    fail_trace = std::strcmp(scenario, "trace-failure") == 0;
    fail_frontier_trace = std::strcmp(scenario, "frontier-trace-failure") == 0;
    if (std::strcmp(scenario, "missing-renderer") != 0) {
        const WwRendererConfig config = {.memory = &host->memory, .cpu = cpu};
        assert(ww_renderer_create(&host->renderer, &config, &backend) == nullptr);
    }
    const StaticRecompRange range = {0x80308A9Cu, 0x80308AA0u};
    const StaticRecompModuleDesc module = {.code_ranges = &range, .num_code_ranges = 1u};
    host->module = std::strcmp(scenario, "unowned") == 0 ? nullptr : &module;
    host->stream.enabled = std::strcmp(scenario, "disabled-stream") != 0;
    host->stream.decoded = 17u;
    cpu->pc = range.start;
    cpu->gpr[1] = 0x8040CEA8u;
    cpu->lr = std::strcmp(scenario, "bad-caller") == 0 ? 0x80322C34u : 0x80322C38u;
    cpu->gpr[3] = std::strcmp(scenario, "bad-queue") == 0 ? 0x803F7B44u : 0x803F7C9Cu;
    cpu->gpr[13] = std::strcmp(scenario, "bad-sda") == 0 ? 0x8040E0E0u : 0x803FE0E0u;
    if (std::strcmp(scenario, "aliased-queue") == 0)
        assert(ppc_guest_alias_add(0x803F7C9Cu, 8u, nullptr));
    cpu->external_write(cpu, DOL_PE_INTERRUPT_STATUS, DOL_PE_FINISH_ENABLE_BIT, 2u);
    const bool masked = std::strcmp(scenario, "masked") == 0;
    cpu->external_write(cpu, DOL_PI_INTERRUPT_MASK, masked ? 0u : DOL_PI_CAUSE_PE_FINISH, 4u);
    if (std::strcmp(scenario, "pending-command") == 0)
        host->stream.command.size = 1u;
    if (std::strcmp(scenario, "pending-gather") == 0)
        host->gather.size = 1u;
    if (std::strcmp(scenario, "pending-fifo") == 0)
        host->cp_fifo_distance = 32u;
    if (std::strcmp(scenario, "pending-capture") == 0)
        host->fifo_capture.count = 1u;
    if (std::strcmp(scenario, "pending-read") == 0)
        host->fifo_capture.read_bytes = 1u;
    const bool flush_padding = std::strncmp(scenario, "flush-padding-", 14u) == 0;
    const bool bad_padding = std::strncmp(scenario, "bad-padding-", 12u) == 0;
    if (flush_padding || bad_padding) {
        host->gather.size = flush_padding ? static_cast<u8>(std::strtoul(scenario + 14u, nullptr, 10)) : 5u;
        assert(host->gather.size > 0u && host->gather.size < 32u);
        for (u32 index = 0u; index < host->gather.size; ++index) {
            host->gather.origins[index].pc = 0x803229C4u - ((host->gather.size - 1u - index) / 4u) * 4u;
            host->gather.origins[index].sp = cpu->gpr[1] - 8u;
        }
        if (std::strcmp(scenario, "bad-padding-byte") == 0)
            host->gather.bytes[0] = 0x61u;
        if (std::strcmp(scenario, "bad-padding-origin") == 0)
            host->gather.origins[0].byte = 0x61u;
        if (std::strcmp(scenario, "bad-padding-pc") == 0)
            host->gather.origins[0].pc = 0x803229C4u;
        if (std::strcmp(scenario, "bad-padding-sp") == 0)
            host->gather.origins[0].sp += 4u;
    }
    const bool new_work = std::strcmp(scenario, "new-work") == 0;
    const bool success = std::strcmp(scenario, "finish") == 0 || std::strcmp(scenario, "repeat") == 0 ||
                         masked || new_work || flush_padding;
    const CPUState before = *cpu;
    const WwGatherPipe gather_before = host->gather;
    const u32 queue_before = mem_read32(cpu, 0x803F7C9Cu);
    assert(ww_host_edge(host, cpu, cpu->pc) == !success);
    assert((host->error == nullptr) == success && host->edge_stopped == !success);
    assert(cpu->pc == before.pc && cpu->lr == before.lr && cpu->msr == before.msr);
    assert(std::memcmp(cpu->gpr, before.gpr, sizeof(cpu->gpr)) == 0);
    assert(host->cycles.absolute_cycles == 0u && mem_read32(cpu, 0x803F7C9Cu) == queue_before);
    assert(std::memcmp(&gather_before, &host->gather, sizeof(gather_before)) == 0);
    assert(host->interrupts.pe_finish_pending == success);
    if (success) {
        assert(backend.finishes == 1u && dol_interrupts_external_pending(&host->interrupts) == !masked);
        if (std::strcmp(scenario, "repeat") == 0) {
            assert(!ww_host_edge(host, cpu, cpu->pc));
            assert(backend.finishes == 1u);
        }
        if (new_work) {
            cpu->external_write(cpu, DOL_PE_INTERRUPT_STATUS, DOL_PE_FINISH_ACK_BIT | DOL_PE_FINISH_ENABLE_BIT, 2u);
            assert(!host->interrupts.pe_finish_pending);
            ++host->stream.decoded;
            assert(!ww_host_edge(host, cpu, cpu->pc));
            assert(backend.finishes == 2u && host->interrupts.pe_finish_pending);
        }
        if (!masked) {
            cpu->msr = PPC_MSR_EE;
            assert(ww_host_edge(host, cpu, cpu->pc));
            assert(cpu->pc == 0x500u && cpu->srr0 == range.start && cpu->exception == 0u);
        }
    } else {
        assert((dol_interrupts_pi_cause(&host->interrupts) & DOL_PI_CAUSE_PE_FINISH) == 0u);
        assert(backend.finishes == (backend.fail || fail_trace ? 1u : 0u));
    }
    ww_host_shutdown(host);
    std::free(host);
    return 0;
}
