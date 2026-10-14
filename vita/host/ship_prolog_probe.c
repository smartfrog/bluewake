#include "probe_support.h"
#include "dispatch_loop.h"
#include "external_vector.h"
#include <stdlib.h>
#include "ship_fixture.inc"
#include "ship_contract.inc"
#include "rel_section_guard_probe.h"
#include "rel_icbi_probe.h"
#define DOLRECOMP_CPU_HEADER "core/cpu.h"
#define DOLRECOMP_C_LOOP_CYCLE_BUDGET (ctx->cycle_budget)
#include "ship_translated.c"

static const char* scenario;
static u32 interrupt_handoffs;
static u32 interrupt_restorations;
static bool restoration_dispatched;

static bool diverted_restoration(void) {
    return strcmp(scenario, "interrupt-diverted") == 0 || strcmp(scenario, "interrupt-reentry-rfi") == 0;
}

static bool pending_entry(void) {
    return strcmp(scenario, "interrupt-reentry-pending") == 0 || strcmp(scenario, "interrupt-return-pending") == 0;
}

static void assert_suspended_admission_cleared(WwHost* host) {
    assert(host->error != NULL && !host->ship_prolog_return_ready && !host->ship_prolog_return_interrupted);
    assert(host->ship_prolog_owner == 0u && !host->ship_prolog_ctor_completed);
    assert(host->ship_prolog_stack == 0u && host->ship_prolog_r2 == 0u && host->ship_prolog_r13 == 0u);
    assert(mem_read32(&host->cpu, 0xC1B224D0u) == 2u);
    assert(ww_host_ship_boundary(host, 0x80240EE8u) != NULL);
    fprintf(stderr, "PASS suspended-admission-cleared reason=%s\n", host->error);
}

static bool interrupt_return_edge(WwHost* host, CPUState* cpu, u32 address) {
    if (interrupt_handoffs != 0u && interrupt_restorations < interrupt_handoffs) {
        assert(cpu->pc == 0x80240EE8u && (cpu->msr & PPC_MSR_EE) != 0u);
        assert(mem_read32(cpu, 0x80006198u) == cpu->pc);
        assert(mem_read32(cpu, 0x8000619Cu) == cpu->msr);
        ++interrupt_restorations;
        if (strcmp(scenario, "interrupt-invalid-sp") == 0) cpu->gpr[1] += 16u;
        if (strcmp(scenario, "interrupt-invalid-r31") == 0) cpu->gpr[31] += 32u;
        if (strcmp(scenario, "interrupt-invalid-lr") == 0) cpu->lr = 0x80001004u;
        if (strcmp(scenario, "interrupt-invalid-r2") == 0) cpu->gpr[2] += 4u;
        if (strcmp(scenario, "interrupt-invalid-r13") == 0) cpu->gpr[13] += 4u;
        const bool stopped = ww_host_edge(host, cpu, address);
        if (strncmp(scenario, "interrupt-invalid-", 18u) == 0) {
            assert(stopped && host->error != NULL);
            fprintf(stderr, "PASS rejected restored-context reason=%s\n", host->error);
            return true;
        }
        if (host->error != NULL)
            fprintf(stderr, "STOP interrupted-return reason=%s\n", host->error);
        assert(!stopped && host->error == NULL && host->ship_prolog_return_ready);
        fprintf(stderr, "[ship-test] stage=interrupt-restored return=%08X\n", address);
    }
    const u32 required = strcmp(scenario, "interrupt-repeated") == 0 ? 2u : 1u;
    if (interrupt_handoffs == required)
        return false;
    const bool external = strcmp(scenario, "interrupt-external") == 0 ||
        (strcmp(scenario, "interrupt-repeated") == 0 && interrupt_handoffs == 0u);
    const u32 vector = external ? WW05_EXTERNAL_VECTOR : WW05_DECREMENTER_VECTOR;
    const u8* reference = external ? ww05_external_vector_reference : ww05_decrementer_vector_reference;
    memcpy(cpu->ram + vector, reference, WW05_EXTERNAL_VECTOR_BYTES);
    mem_write32(cpu, 0x800000C0u, 0x80006000u);
    mem_write32(cpu, 0x80003010u, 0x80007000u);
    cpu->msr |= PPC_MSR_EE;
    if (external) {
        cpu->external_write(cpu, DOL_PI_INTERRUPT_MASK, DOL_PI_CAUSE_SI, 4u);
        cpu->external_write(cpu, DOL_SI_BASE + DOL_SI_COMCSR_OFF, 0xC0010301u, 4u);
    } else {
        host->decrementer_pending = true;
    }
    ++interrupt_handoffs;
    assert(ww_host_edge(host, cpu, address));
    assert(host->error == NULL && cpu->pc == vector && cpu->srr0 == address && cpu->exception == 0u);
    assert(host->ship_prolog_return_ready);
    if (external)
        cpu->external_write(cpu, DOL_SI_BASE + DOL_SI_COMCSR_OFF, 0xC0010300u, 4u);
    fprintf(stderr, "[ship-test] stage=interrupt-delivered vector=%08X return=%08X\n", vector, address);
    return true;
}

static bool probe_edge(void* user, CPUState* cpu, u32 address) {
    WwHost* host = user;
    const u32 canonical = address & ~0x40000000u;
    if (restoration_dispatched && diverted_restoration())
        assert_suspended_admission_cleared(host);
    if (canonical == 0x80240EE8u && strncmp(scenario, "interrupt-", 10u) == 0)
        return interrupt_return_edge(host, cpu, address);
    if (canonical == 0x80241178u) {
        if (strcmp(scenario, "ctor-mutated") == 0)
            mem_write32(cpu, 0xC1B1DAF0u, 0x80001004u);
        if (strcmp(scenario, "raw-mutated") == 0) {
            WwRelImage* image = ww_host_rel_live(host, 168u);
            assert(image != NULL);
            mem_write8(cpu, image->guest + 0x1C7u, 1u);
        }
    }
    return ww_host_edge(user, cpu, address);
}

static int translated_chunk(CPUState* cpu, u32 address) {
    switch (address & ~0x40000000u) {
    case 0x80007000u:
        if (diverted_restoration()) {
            const u32 target = strcmp(scenario, "interrupt-diverted") == 0 ? 0x80240EECu : 0xC1B100F4u;
            mem_write32(cpu, 0x80006198u, target);
            func_80007000(cpu);
            assert(cpu->pc == target);
            restoration_dispatched = true;
            fprintf(stderr, "[ship-test] stage=restoration-dispatched target=%08X\n", cpu->pc);
            return 1;
        }
        func_80007000(cpu); return 1;
    case 0x80240EC0u: case 0x80240ED8u: case 0x80240EE8u: case 0x80240EECu:
        func_80240EC0(cpu); return 1;
    case 0x80241178u: case 0x80241198u: case 0x802411ACu: case 0x802411B4u:
        func_C0241178(cpu); return 1;
    case 0x802410B4u: func_C02410B4(cpu); return 1;
    case 0x81B100F4u: case 0x81B1010Cu: case 0x81B10110u:
        func_C1B100F4(cpu); return 1;
    case 0x81B1D414u: case 0x81B1D424u: case 0x81B1D428u:
        func_C1B1D414(cpu); return 1;
    case 0x81B1016Cu: func_C1B1016C(cpu); return 1;
    default: return 0;
    }
}

static int translated_dispatch(CPUState* cpu, u32 address) {
    return bluewake_chassis_dispatch_loop(cpu, address, translated_chunk, probe_edge, cpu->external_user_data);
}

int main(int argc, char** argv) {
    assert(argc >= 2 && argc <= 4);
    scenario = argv[1];
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    StaticRecompRelModule rel = fixture_rel;
    if (strcmp(scenario, "other-actor") == 0)
        rel.module_id = 169u;
    StaticRecompModuleDesc module = {.code_ranges = fixture_ranges,
        .num_code_ranges = sizeof(fixture_ranges) / sizeof(fixture_ranges[0]),
        .chunk_ranges = fixture_chunks, .chunk_hashes = fixture_hashes, .num_chunk_ranges = 3u,
        .rel_modules = &rel, .num_rel_modules = 1u, .dispatch = translated_dispatch};
    host->module = &module;
    host->rel_data = fixture_data;
    host->rel_data_count = 2u;
    StaticRecompRange* scratch_ranges = NULL;
    if (argc >= 3) {
        assert(strncmp(scenario, "scratch-", 8u) == 0 || strncmp(scenario, "icbi-", 5u) == 0 ||
               strncmp(scenario, "sections-", 9u) == 0);
        FILE* stream = fopen(argv[2], "rb");
        assert(stream != NULL);
        u8 word[4];
        assert(fread(word, 1u, 4u, stream) == 4u);
        const u32 count = read_be32(word);
        assert(count != 0u && count <= 2048u);
        scratch_ranges = calloc(module.num_code_ranges + count, sizeof(*scratch_ranges));
        assert(scratch_ranges != NULL);
        memcpy(scratch_ranges, fixture_ranges, sizeof(fixture_ranges));
        for (u32 at = 0u; at < count; ++at) {
            u8 pair[8];
            assert(fread(pair, 1u, sizeof(pair), stream) == sizeof(pair));
            const u32 start = read_be32(pair), end = read_be32(pair + 4u);
            assert(start < end);
            scratch_ranges[module.num_code_ranges + at] = (StaticRecompRange){start, end};
        }
        assert(fgetc(stream) == EOF && !ferror(stream));
        assert(fclose(stream) == 0);
        module.code_ranges = scratch_ranges;
        module.num_code_ranges += count;
    }
    if (strcmp(scenario, "missing-contract") == 0) {
        const char* error = ww_host_enable_ship_contract(host, NULL, "synthetic.dol");
        assert(error != NULL && host->ship_contract == NULL);
        fprintf(stderr, "PASS missing-contract reason=%s\n", error);
    }
    if (strcmp(scenario, "default-off") != 0 && strcmp(scenario, "missing-contract") != 0) {
        BlueWakeShipContractV1 contract = s_ship_contract;
        static u8 raw[sizeof(s_ship_raw_rel)];
        memcpy(raw, s_ship_raw_rel, sizeof(raw));
        if (strncmp(scenario, "uncertified-", 12u) == 0) {
            const u32 offset = strcmp(scenario, "uncertified-prolog") == 0 ? 0x34u :
                strcmp(scenario, "uncertified-epilog") == 0 ? 0x38u : 0x3Cu;
            write_be32(raw + offset, read_be32(raw + offset) + 4u);
            contract.rel_bytes = raw;
        }
        if (strcmp(scenario, "lifecycle-data") == 0) {
            write_be32(raw + 0x54u, 0x80u);
            contract.rel_bytes = raw;
        }
        if (argc == 4) {
            char* end = NULL;
            const unsigned long fix_size = strtoul(argv[3], &end, 0);
            assert(end != argv[3] && *end == '\0' && fix_size <= UINT32_MAX);
            write_be32(raw + 0x48u, (u32)fix_size);
            contract.rel_bytes = raw;
        }
        static BlueWakeShipContractV1 enabled_contract;
        enabled_contract = contract;
        const char* error = ww_host_enable_ship_contract(host, &enabled_contract, "synthetic.dol");
        if (strcmp(scenario, "bad-dol") == 0) {
            assert(error != NULL && host->ship_contract == NULL);
            fprintf(stderr, "PASS rejected reason=%s\n", error);
            ww_host_shutdown(host);
            free(host);
            return 0;
        }
        assert(error == NULL);
    }
    u64 hashes[3];
    memcpy(hashes, fixture_hashes, sizeof(hashes));
    if (strcmp(scenario, "stale-code") == 0) {
        hashes[0] ^= 1u;
        module.chunk_hashes = hashes;
    }
    for (u32 at = 0u; at < 2u; ++at)
        assert(ppc_guest_alias_add(fixture_data[at].linked_start, fixture_data[at].size, fixture_data[at].bytes));
    if (strcmp(scenario, "scratch-alias") == 0)
        assert(ppc_guest_alias_add(0xC1F90000u, 32u, NULL));
    if (strcmp(scenario, "scratch-live-prefix") == 0) {
        host->rel_images[0] = (WwRelImage){.owner = 0x80002300u, .guest = 0x81F90000u,
            .capacity = 0x10020u, .module_id = 999u};
        host->rel_image_count = 1u;
        host->alias_count = 1u;
        host->alias_bytes = 0x10020u;
        assert(ppc_guest_alias_add(0x81F90000u, 0x10020u, NULL));
        mem_write32(&host->cpu, 0x81F90000u, 0x12345678u);
    }
    CPUState* cpu = &host->cpu;
    cpu->gpr[1] = 0x80004000u;
    cpu->gpr[2] = 0x80300000u;
    cpu->gpr[13] = 0x80400000u;
    cpu->gpr[31] = 0x12345678u;
    cpu->gpr[3] = 0x80002100u;
    cpu->lr = 0x80001004u;
    mem_write32(cpu, 0x80002100u, 0x80002200u);
    mem_write32(cpu, 0x8000211Cu, 0x80003000u);
    memcpy(cpu->ram + 0x3000u, strcmp(scenario, "other-actor") == 0 ? "d_a_test" : "d_a_ship", 9u);
    cpu->pc = 0x80240EC0u;
    cpu->cycle_budget = 200;
    if (strncmp(scenario, "sections-", 9u) == 0) {
        char site[32];
        const char* form = strchr(scenario + 9u, ':');
        assert(form != NULL && (size_t)(form - scenario - 9u) < sizeof(site));
        memcpy(site, scenario + 9u, (size_t)(form - scenario - 9u));
        site[form - scenario - 9u] = '\0';
        probe_rel_sections(host, site, form + 1u);
        ww_host_shutdown(host);
        free(scratch_ranges);
        free(host);
        return 0;
    }
    if (strncmp(scenario, "icbi-", 5u) == 0) {
        probe_rel_icbi(host, scenario);
        ww_host_shutdown(host);
        free(scratch_ranges);
        free(host);
        return 0;
    }
    for (u32 turn = 0u; turn < 32u && cpu->pc != 0x80001004u && host->error == NULL; ++turn) {
        if (cpu->pc == 0x80007000u && pending_entry()) {
            assert(host->ship_prolog_return_interrupted && host->ship_prolog_return_ready);
            assert(cpu->lr == 0x80240EE8u && cpu->gpr[31] == host->ship_prolog_owner);
            assert(cpu->gpr[1] == host->ship_prolog_stack);
            cpu->pc = strcmp(scenario, "interrupt-reentry-pending") == 0 ? 0xC1B100F4u : 0x80240EE8u;
            const char* error = ww_host_ship_boundary(host, cpu->pc);
            assert(error != NULL);
            ww_host_fail(host, error, cpu->pc, cpu->pc);
            break;
        }
        if (cpu->pc == 0x80240EE8u && interrupt_restorations < interrupt_handoffs &&
            interrupt_return_edge(host, cpu, cpu->pc))
            continue;
        if ((cpu->pc & ~0x40000000u) == 0x81B100F4u) {
            if (strcmp(scenario, "entry-lr") == 0) cpu->lr = 0x80001004u;
            if (strcmp(scenario, "entry-r31") == 0) cpu->gpr[31] = 0x80002200u;
        }
        int result = ww_host_dispatch(host, cpu->pc);
        if (result == 0) {
            ww_host_fail(host, "synthetic translated dispatcher has no instruction", cpu->pc, cpu->pc);
            break;
        }
    }
    if (host->error != NULL)
        fprintf(stderr, "STOP pc=%08X lr=%08X sp=%08X r31=%08X ctor-owner=%08X reason=%s\n",
                cpu->pc, cpu->lr, cpu->gpr[1], cpu->gpr[31], host->rel_constructor_owner, host->error);
    if (diverted_restoration() || pending_entry()) {
        assert(pending_entry() || restoration_dispatched);
        assert_suspended_admission_cleared(host);
        ww_host_shutdown(host);
        free(host);
        return 0;
    }
    if (strcmp(scenario, "default-off") == 0 || strcmp(scenario, "missing-contract") == 0 ||
        strcmp(scenario, "bad-rel") == 0 || strcmp(scenario, "stale-code") == 0 ||
        strcmp(scenario, "other-actor") == 0 || strcmp(scenario, "lifecycle-data") == 0 ||
        strncmp(scenario, "uncertified-", 12u) == 0) {
        assert(host->error != NULL && cpu->pc == 0x80240744u);
        assert(mem_read32(cpu, 0x80002110u) == 0u && host->alias_count == 0u);
        fprintf(stderr, "PASS rejected-before-publication pc=%08X\n", cpu->pc);
        ww_host_shutdown(host);
        free(host);
        return 0;
    }
    if (strcmp(scenario, "scratch-exhausted") == 0 || strcmp(scenario, "scratch-alias") == 0 ||
        strcmp(scenario, "scratch-fix-rejected") == 0) {
        assert(host->error != NULL && cpu->pc == 0x80240744u);
        assert(mem_read32(cpu, 0x80002110u) == 0u && host->alias_count == 0u);
        assert(host->alias_bytes == 0u && host->rel_image_count == 0u);
        assert(ww_host_rel_owned(host, 0x80002100u) == NULL);
        if (strcmp(scenario, "scratch-fix-rejected") == 0) {
            assert(!ww_host_linked_alias_overlaps(0x81FE0000u, 74976u));
            assert(!ww_host_linked_alias_overlaps(0xC1FE0000u, 74976u));
        }
        assert(mem_read32(cpu, 0x80002100u) == 0x80002200u && mem_read32(cpu, 0x8000211Cu) == 0x80003000u);
        fprintf(stderr, "PASS rejected-before-publication reason=%s\n", host->error);
        ww_host_shutdown(host);
        free(scratch_ranges);
        free(host);
        return 0;
    }
    if (strcmp(scenario, "ctor-mutated") == 0 || strcmp(scenario, "raw-mutated") == 0 ||
        strncmp(scenario, "entry-", 6u) == 0 || strncmp(scenario, "interrupt-invalid-", 18u) == 0) {
        assert(host->error != NULL);
        assert(mem_read32(cpu, 0xC1B224D0u) == (strncmp(scenario, "interrupt-", 10u) == 0 ? 2u : 0u));
        fprintf(stderr, "PASS rejected stage=%s\n",
                strncmp(scenario, "interrupt-invalid-", 18u) == 0 ? "restored-return" : "before-constructor");
        ww_host_shutdown(host);
        free(host);
        return 0;
    }
    assert(host->error == NULL);
    assert(cpu->pc == 0x80001004u && cpu->lr == cpu->pc);
    assert(cpu->gpr[1] == 0x80004000u && cpu->gpr[31] == 0x12345678u);
    assert(cpu->gpr[2] == 0x80300000u && cpu->gpr[13] == 0x80400000u);
    assert(mem_read32(cpu, 0xC1B224D0u) == 2u);
    assert(cpu->ctr == 0xC1B1D414u);
    fprintf(stderr, "PASS caller-returned pc=%08X bss-init-calls=2\n", cpu->pc);
    WwRelImage* image = ww_host_rel_live(host, 168u);
    assert(image != NULL && host->ship_prolog_owner == 0u && host->rel_constructor_owner == 0u);
    if (strncmp(scenario, "scratch-", 8u) == 0) {
        const u32 expected = strcmp(scenario, "scratch-tagged-prefix") == 0 ? 0x81F90040u :
            strcmp(scenario, "scratch-live-prefix") == 0 ? 0x81FA0020u :
            strcmp(scenario, "scratch-fix-safe") == 0 ? 0x81FE0000u : 0x81F90000u;
        assert(image->guest == expected && image->bytes == 74960u && image->capacity == 74976u);
        assert(image->guest >= 0x81F90000u && (u64)image->guest + image->capacity <= 0x82000000u);
        const u32 fix_size = mem_read32(cpu, image->guest + 0x48u);
        assert(fix_size == read_be32(host->ship_contract->rel_bytes + 0x48u));
        assert((u64)image->guest + fix_size < 0x82000000u);
        fprintf(stderr, "PASS fix-size raw=%08X staged=%08X\n",
                read_be32(host->ship_contract->rel_bytes + 0x48u), fix_size);
        if (strcmp(scenario, "scratch-live-prefix") == 0) {
            assert(mem_read32(cpu, 0x81F90000u) == 0x12345678u);
            assert(host->rel_images[0].guest == 0x81F90000u && host->rel_images[0].capacity == 0x10020u);
        }
        u8* storage = NULL;
        assert(ppc_guest_alias_get_storage(image->guest, image->capacity, &storage));
        assert(get_ram_ptr(cpu, image->guest, image->bytes, NULL) == storage);
        assert(get_ram_ptr(cpu, image->guest | 0x40000000u, image->bytes, NULL) == storage);
        assert(mem_read32(cpu, 0x80002100u) == 0x80002200u && mem_read32(cpu, 0x8000211Cu) == 0x80003000u);
        fprintf(stderr, "PASS staging guest=%08X bytes=%u capacity=%u controller=preserved\n",
                image->guest, image->bytes, image->capacity);
    }
    if (strncmp(scenario, "interrupt-", 10u) == 0)
        assert(interrupt_restorations == interrupt_handoffs && interrupt_restorations != 0u);
    if (strcmp(scenario, "interrupt-expiry") == 0) {
        assert(!host->ship_prolog_return_ready);
        fprintf(stderr, "PASS admission-expired pc=%08X\n", cpu->pc);
    }
    if (strcmp(scenario, "resume-without-prolog") == 0 || strcmp(scenario, "interrupt-expiry") == 0) {
        cpu->pc = cpu->lr = 0x80240EE8u;
        cpu->gpr[31] = image->owner;
        assert(ww_host_edge(host, cpu, cpu->pc) && host->error != NULL);
        fprintf(stderr, "PASS rejected reason=%s\n", host->error);
    } else if (strcmp(scenario, "relink") == 0) {
        const u32 prolog = mem_read32(cpu, image->guest + 0x34u);
        assert(ww_host_link_rel(host, image->owner) == NULL);
        assert(mem_read32(cpu, 0xC1B224D0u) == 2u && mem_read32(cpu, image->guest + 0x34u) == prolog);
        assert(ww_host_rel_raw_unchanged(image));
        fprintf(stderr, "PASS relink-preserved\n");
    } else if (strcmp(scenario, "unlink") == 0) {
        const char* error = ww_host_unlink_rel(host, image->owner);
        assert(error != NULL && image->linked && !image->unlinked);
        assert(mem_read32(cpu, image->owner + 0x10u) == image->guest);
        fprintf(stderr, "PASS rejected reason=%s\n", error);
    } else if (strncmp(scenario, "epilog-", 7u) == 0 || strncmp(scenario, "unresolved-", 11u) == 0 ||
               strcmp(scenario, "native-unlink") == 0) {
        const bool epilog = strncmp(scenario, "epilog-", 7u) == 0;
        cpu->pc = epilog ? 0xC1B10120u : 0xC1B1014Cu;
        if (strstr(scenario, "scratch") != NULL)
            cpu->pc = mem_read32(cpu, image->guest + (epilog ? 0x38u : 0x3Cu));
        if (strcmp(scenario, "native-unlink") == 0) {
            cpu->pc = 0x80240F54u;
            cpu->gpr[3] = image->owner;
        }
        assert(ww_host_dispatch(host, cpu->pc) == -1 && host->error != NULL && image->linked);
        fprintf(stderr, "PASS rejected reason=%s\n", host->error);
    } else if (strncmp(scenario, "return-", 7u) == 0) {
        cpu->lr = 0x80240EE8u;
        cpu->gpr[31] = image->owner;
        assert(ww_host_ship_boundary(host, 0xC1B100F4u) == NULL);
        if (strcmp(scenario, "return-without-ctor") != 0) {
            cpu->gpr[3] = 0xC1B1DAF0u;
            cpu->lr = 0xC1B1010Cu;
            assert(ww_host_rel_constructor_boundary(host, 0x80241178u) == NULL);
            assert(ww_host_rel_constructor_boundary(host, 0xC1B1010Cu) == NULL);
            cpu->lr = 0x80240EE8u;
        }
        if (strcmp(scenario, "return-sp") == 0) cpu->gpr[1] += 16u;
        if (strcmp(scenario, "return-r31") == 0) cpu->gpr[31] += 32u;
        if (strcmp(scenario, "return-lr") == 0) cpu->lr = 0x80001004u;
        if (strcmp(scenario, "return-r2") == 0) cpu->gpr[2] += 4u;
        if (strcmp(scenario, "return-r13") == 0) cpu->gpr[13] += 4u;
        const char* error = ww_host_ship_boundary(host, 0x80240EE8u);
        assert(error != NULL && host->ship_prolog_owner == image->owner);
        fprintf(stderr, "PASS rejected reason=%s\n", error);
    }
    ww_host_shutdown(host);
    free(scratch_ranges);
    free(host);
    return 0;
}
