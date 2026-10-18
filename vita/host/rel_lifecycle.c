#include "boot_host.h"
#if WW05_OBSERVE_NATIVE_CTX || WW05_DISPATCH_ABI_SENTINEL
#include "native_ctx.h"
#endif

#define WW_REL_CALLER_SUCCESSOR 0x80240EE8u

void ww_host_rel_lifecycle_revoke(WwHost* host) {
    ww_host_rel_profile_revoke(host);
    memset(&host->rel_pending_init, 0, sizeof(host->rel_pending_init));
    memset(&host->rel_pending_unlink, 0, sizeof(host->rel_pending_unlink));
    host->rel_constructor_owner = 0u;
    host->rel_constructor_return = 0u;
    host->rel_constructor_table = 0u;
    host->rel_constructor_size = 0u;
    host->rel_constructor_bytes = NULL;
    host->rel_constructor_observation_open = false;
}

static bool policy_permits(const WwHost* host, const WwRelImage* image) {
    if (host->ship_contract != NULL && image->certificate == &host->ship_certificate)
        return true;
#if WW05_ENABLE_IKADA_CONTRACT
    if (host->rel_contracts != NULL)
#else
    if (host->rel_title_admission && host->rel_contracts != NULL)
#endif
        for (u32 at = 0u; at < host->rel_contracts->contract_count; ++at)
#if WW05_ENABLE_IKADA_CONTRACT
            if (((host->rel_title_admission && host->rel_contracts->contracts[at].module_id == 400u) ||
                 (host->rel_ikada_admission && host->rel_title_admission && host->ship_contract != NULL &&
                  host->rel_contracts->contracts[at].module_id == 60u)) &&
#else
            if (host->rel_contracts->contracts[at].module_id == 400u &&
#endif
                image->certificate == &host->rel_contracts->contracts[at])
                return true;
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    const BlueWakeRelInitContractV1* actor = ww_host_actor_batch_certificate(host, image->module_id);
    if (actor != NULL && actor == image->certificate)
        return true;
#endif
#if WW05_TESTS_ONLY
    if (host->rel_test_admission && host->rel_contracts != NULL)
        for (u32 at = 0u; at < host->rel_contracts->contract_count; ++at)
            if (image->certificate == &host->rel_contracts->contracts[at])
                return true;
#endif
    return false;
}

static bool live_identity(WwHost* host, const WwRelPendingInit* pending) {
    const WwRelImage* image = pending->image;
    return image != NULL && ww_host_rel_owned(host, pending->owner) == image &&
        ww_host_rel_live(host, pending->module_id) == image && image->certificate == pending->certificate &&
        pending->certificate != NULL && pending->certificate->rel_bytes == pending->identity &&
        pending->certificate->rel_size == pending->bytes && image->module_id == pending->module_id &&
        image->owner == pending->owner && image->guest == pending->guest && image->bytes == pending->bytes &&
        image->capacity == pending->capacity && image->sections == pending->sections && policy_permits(host, image) &&
        image->sdk_phase == WW_REL_SDK_CERTIFIED && ww_host_rel_sections_match_post_link(image) &&
        ww_host_rel_raw_unchanged(image) && ww_host_rel_sdk_integrity(host);
}

static bool caller_matches(const WwHost* host) {
    const WwRelPendingInit* pending = &host->rel_pending_init;
    const CPUState* cpu = &host->cpu;
    return cpu->gpr[1] == pending->r1 && cpu->gpr[2] == pending->r2 && cpu->gpr[13] == pending->r13 &&
        cpu->gpr[31] == pending->r31 && pending->r31 == pending->owner && cpu->lr == pending->successor;
}

bool ww_host_rel_unlink_identity(WwHost* host, const WwRelPendingUnlink* pending) {
    const WwRelImage* image = pending->image;
    return image != NULL && ww_host_rel_owned(host, pending->owner) == image &&
        ww_host_rel_live(host, pending->module_id) == image && image->certificate == pending->certificate &&
        pending->certificate != NULL && pending->certificate->rel_bytes == pending->identity &&
        pending->certificate->rel_size == pending->bytes && image->module_id == pending->module_id &&
        image->owner == pending->owner && image->guest == pending->guest && image->bytes == pending->bytes &&
        image->capacity == pending->capacity && image->sections == pending->sections &&
        policy_permits(host, image) && image->sdk_phase == WW_REL_SDK_CERTIFIED &&
        ww_host_rel_sections_match_post_link(image) && ww_host_rel_raw_unchanged(image) &&
        ww_host_rel_sdk_integrity(host);
}

const char* ww_host_rel_unlink_entry(WwHost* host, u32 address) {
    CPUState* cpu = &host->cpu;
    WwRelImage* image = ww_host_rel_owned(host, cpu->gpr[3]);
    if (image == NULL)
        return NULL;
    if ((address & ~0x40000000u) != 0x80240F54u || cpu->pc != address)
        return "REL native unlink requires its exact translated entry pc";
    if (host->rel_pending_unlink.phase != WW_REL_UNLINK_NONE) {
        const WwRelPendingUnlink* pending = &host->rel_pending_unlink;
        if (pending->phase == WW_REL_UNLINK_ENTRY_READY && pending->image == image &&
            pending->owner == cpu->gpr[3] && cpu->gpr[3] == cpu->gpr[31] &&
            pending->guest == image->guest)
            return NULL;
        return "REL native unlink requires no pending unlink";
    }
    if (host->rel_pending_init.phase != WW_REL_INIT_NONE)
        return "REL native unlink requires no pending initialization";
    if (cpu->gpr[3] != cpu->gpr[31])
        return "REL native unlink requires its exact owner in r3 and r31";
    if (cpu->lr != 0x80240314u)
        return "REL native unlink requires its translated vtable caller";
    if (image->module_id == 1u)
        return "REL profile-list resident contract does not support unload";
    if (!image->linked || ww_host_rel_live(host, image->module_id) != image)
        return "REL native unlink requires unchanged live ownership";
    if (!image->initialization_completed)
        return "REL native unlink requires its completed initialization";
    if (image->certificate == NULL || image->sdk_phase != WW_REL_SDK_CERTIFIED)
        return "REL native unlink requires its certified destruction contract";
    if (!policy_permits(host, image))
        return "REL native unlink requires its admitted certificate policy";
    if (ww_rel_validate_certificate(host, image->certificate) != NULL)
        return "REL native unlink requires its valid certificate";
    if (!ww_host_rel_sections_match_post_link(image) || !ww_host_rel_raw_unchanged(image))
        return "REL native unlink requires unchanged raw and section state";
    if (!ww_host_rel_sdk_integrity(host))
        return "REL native unlink requires the intact SDK list";
    for (u32 at = 0u; at < host->rel_image_count; ++at) {
        const WwRelImage* importer = &host->rel_images[at];
        if (!importer->linked)
            continue;
        for (u32 dependency = 0u; dependency < importer->dependency_count; ++dependency)
            if (importer->dependencies[dependency] == image->module_id)
                return "REL native unlink would invalidate a live importing module";
    }
    if (fprintf(stderr, "[rel-unlink] module=%u stage=entered pc=%08X owner=%08X\n",
                image->module_id, address, image->owner) < 0)
        return "REL native unlink entry trace failed";
    host->rel_pending_unlink = (WwRelPendingUnlink){.phase = WW_REL_UNLINK_ENTRY_READY, .image = image,
        .certificate = image->certificate, .identity = image->certificate->rel_bytes,
        .sections = image->sections, .module_id = image->module_id, .owner = image->owner,
        .guest = image->guest, .bytes = image->bytes, .capacity = image->capacity, .r31 = cpu->gpr[31],
        .callback = mem_read32(cpu, cpu->gpr[3] + 60u)};
    return NULL;
}

static const char* unlink_epilog(WwHost* host, WwRelImage* image, u32 canonical) {
    WwRelPendingUnlink* pending = &host->rel_pending_unlink;
    const BlueWakeRelLifecycleV2* lifecycle = image->certificate->lifecycle;
    if (canonical != (lifecycle->epilog_address & ~0x40000000u) ||
        pending->phase == WW_REL_UNLINK_NONE || pending->image != image)
        return "REL epilog or unresolved entry has no supported lifecycle contract";
    CPUState* cpu = &host->cpu;
    if (pending->phase != WW_REL_UNLINK_ENTRY_READY) {
        if (pending->phase == WW_REL_UNLINK_EPILOG_ACTIVE && cpu->lr == 0x80240F80u &&
            cpu->gpr[3] == image->guest)
            return NULL;
        return "REL epilog entry requires its admitted native unlink contract";
    }
    if (cpu->lr != 0x80240F80u)
        return "REL epilog entry requires its translated do_unlink successor";
    if (cpu->gpr[3] != image->guest)
        return "REL epilog entry requires its owned module header";
    if (!ww_host_rel_unlink_identity(host, pending))
        return "REL epilog entry lost its exact live owner";
    if (fprintf(stderr, "[rel-unlink] module=%u stage=epilog-entered pc=%08X owner=%08X\n",
                image->module_id, canonical, image->owner) < 0)
        return "REL unlink epilog entry trace failed";
    pending->phase = WW_REL_UNLINK_EPILOG_ACTIVE;
    return NULL;
}

static const char* unlink_epilog_return(WwHost* host, u32 address) {
    WwRelPendingUnlink* pending = &host->rel_pending_unlink;
    CPUState* cpu = &host->cpu;
    if (pending->phase == WW_REL_UNLINK_EPILOG_RETURNED && pending->image != NULL &&
        address == 0x80240F80u && cpu->pc == address)
        return NULL;
    if (pending->phase != WW_REL_UNLINK_EPILOG_ACTIVE || pending->image == NULL)
        return "REL unlink continuation requires its admitted epilog call";
    if (cpu->pc != address)
        return "REL unlink continuation requires its exact translated successor";
    if (!ww_host_rel_unlink_identity(host, pending))
        return "REL unlink epilog return lost its exact live owner";
    if (!pending->destructor_completed)
        return "REL unlink epilog return requires its completed destructor walk";
    if (fprintf(stderr, "[rel-unlink] module=%u stage=epilog-returned pc=%08X owner=%08X\n",
                pending->module_id, address, pending->owner) < 0)
        return "REL unlink epilog return trace failed";
    /* do_unlink's frame is the caller the SDK-unlink entry will use. Save it
     * before the guest runs 0x803056BC, whose prologue writes through r1. */
    pending->r1 = cpu->gpr[1];
    pending->r2 = cpu->gpr[2];
    pending->r13 = cpu->gpr[13];
    pending->r31 = cpu->gpr[31];
    pending->lr = cpu->lr;
    pending->phase = WW_REL_UNLINK_EPILOG_RETURNED;
    return NULL;
}

bool ww_host_rel_return_qualified(WwHost* host, u32 address) {
    if (ww_host_rel_profile_return_qualified(host, address))
        return true;
    const WwRelPendingInit* pending = &host->rel_pending_init;
    return pending->phase == WW_REL_INIT_RETURN_READY && address == pending->successor &&
        host->cpu.pc == address && pending->constructor_completed && host->rel_constructor_owner == 0u &&
        caller_matches(host) && live_identity(host, pending);
}

static const char* boundary(WwHost* host, u32 address) {
    WwRelPendingInit* pending = &host->rel_pending_init;
    CPUState* cpu = &host->cpu;
    const u32 canonical = address & ~0x40000000u;
    if (host->rel_pending_unlink.phase == WW_REL_UNLINK_SDK_RETURNED && canonical != 0x80240F8Cu)
        memset(&host->rel_pending_unlink, 0, sizeof(host->rel_pending_unlink));
    const char* profile_error = ww_host_rel_profile_lifecycle(host, address);
    if (profile_error != NULL)
        return profile_error;
    if (pending->phase == WW_REL_INIT_NONE && ww_host_rel_profile_return_qualified(host, address))
        return NULL;
    WwRelImage* entering = NULL;
    WwRelImage* epilog = NULL;
    for (u32 at = 0u; at < host->rel_image_count; ++at) {
        WwRelImage* image = &host->rel_images[at];
        const BlueWakeRelInitContractV1* certificate = image->certificate;
        if (certificate == NULL || certificate->lifecycle == NULL)
            continue;
        const BlueWakeRelLifecycleV2* lifecycle = certificate->lifecycle;
        if (canonical == (lifecycle->unresolved_address & ~0x40000000u))
            return "REL epilog or unresolved entry has no supported lifecycle contract";
        if (canonical == (lifecycle->epilog_address & ~0x40000000u)) {
            if (epilog != NULL)
                return "REL epilog entry ownership is ambiguous";
            epilog = image;
        }
        if (canonical == (lifecycle->prolog_address & ~0x40000000u)) {
            if (entering != NULL)
                return "REL initialization entry ownership is ambiguous";
            entering = image;
        }
    }
    if (canonical == 0x80240F80u)
        return unlink_epilog_return(host, address);
    if (pending->phase == WW_REL_INIT_RETURN_SUSPENDED) {
        if (cpu->exception != 0u)
            return "REL suspended return does not support a nested exception";
        if (entering != NULL)
            return "REL prolog entry is forbidden while its completed return is suspended";
        if (canonical == pending->successor)
            return "REL return requires its verified exception restoration";
    }
    if (pending->phase != WW_REL_INIT_NONE && !live_identity(host, pending))
        return "REL prolog continuation lost its exact live owner";
    if (pending->phase == WW_REL_INIT_RETURN_READY && canonical != pending->successor) {
        if (entering == pending->image)
            return "REL initialization cannot reenter a completed image";
        ww_host_rel_lifecycle_revoke(host);
    }
    if (pending->phase == WW_REL_INIT_ACTIVE && canonical != pending->entry_pc)
        pending->entry_observation_open = false;
    if (canonical == WW_REL_CALLER_SUCCESSOR) {
        if (pending->phase == WW_REL_INIT_NONE)
            return "REL prolog return requires its completed translated call";
        if (address != pending->successor || cpu->pc != pending->successor ||
            !caller_matches(host) || !pending->constructor_completed ||
            host->rel_constructor_owner != 0u)
            return "REL prolog return did not preserve its translated caller";
        if (pending->phase == WW_REL_INIT_ACTIVE) {
            if (fprintf(stderr, "[rel-prolog] module=%u stage=caller-returned pc=%08X owner=%08X\n",
                        pending->module_id, address, pending->owner) < 0)
                return "REL prolog return trace failed";
            pending->image->initialization_completed = true;
            pending->phase = WW_REL_INIT_RETURN_READY;
#if WW05_DISPATCH_ABI_SENTINEL
            if (pending->module_id == 168u)
                ww_abi_arm(pending->module_id, (uint32_t)(uintptr_t)host->module->dispatch);
#endif
#if WW05_OBSERVE_NATIVE_CTX
            if (pending->module_id == 400u)
                ww_native_arm_title(pending->module_id);
#endif
        }
        return NULL;
    }
    if (epilog != NULL) {
        const char* unlink_error = unlink_epilog(host, epilog, canonical);
        if (unlink_error != NULL)
            return unlink_error;
        return NULL;
    }
    if (entering == NULL)
        return NULL;
    const BlueWakeRelInitContractV1* certificate = entering->certificate;
    if (!policy_permits(host, entering))
        return !host->rel_title_admission && certificate->module_id == 400u ? "title initialization is disabled" :
            "REL prolog entry requires its exact translated dynamic-link caller";
    if (ww_host_rel_live(host, entering->module_id) != entering ||
        ww_host_rel_owned(host, entering->owner) != entering || certificate->module_id != entering->module_id ||
        entering->bytes != certificate->rel_size || entering->initialization_completed ||
        cpu->lr != WW_REL_CALLER_SUCCESSOR || cpu->gpr[31] != entering->owner || cpu->gpr[3] != entering->guest ||
        (ww_host_rel_address(host, cpu->pc) & ~0x40000000u) != canonical ||
        ww_rel_validate_certificate(host, certificate) != NULL)
        return "REL prolog entry requires its exact translated dynamic-link caller";
    if (pending->phase != WW_REL_INIT_NONE) {
        if (pending->phase != WW_REL_INIT_ACTIVE || pending->image != entering || !pending->entry_observation_open ||
            !caller_matches(host) || pending->constructor_completed || host->rel_constructor_owner != 0u)
            return "REL prolog does not support a nested or progressed entry";
        return NULL;
    }
    if (host->rel_constructor_owner != 0u)
        return "REL prolog entry requires no outstanding constructor walk";
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    if (ww_host_actor_sdk_contract(host, entering->module_id) == WW_ACTOR_SDK_CERTIFIED_INLINE_ZERO &&
        !ww_host_rel_actor_accounted(host, entering))
        return "REL actor prolog requires its completed fixed shrink and accounting sequence";
#endif
    if (ww_host_rel_profile_enabled(host) && entering->module_id == 400u &&
        !ww_host_rel_title_accounted(host, entering))
        return "REL title prolog requires its completed fixed shrink and accounting sequence";
    const char* error = ww_host_rel_sdk_finish(host, entering);
    if (error != NULL)
        return error;
    if (entering->sdk_phase != WW_REL_SDK_CERTIFIED || !ww_host_rel_sdk_integrity(host))
        return "REL prolog entry requires its exact translated dynamic-link caller";
    if (fprintf(stderr, "[rel-prolog] module=%u stage=entered pc=%08X owner=%08X lr=%08X\n",
                entering->module_id, address, entering->owner, cpu->lr) < 0)
        return "REL prolog entry trace failed";
    *pending = (WwRelPendingInit){.phase = WW_REL_INIT_ACTIVE, .image = entering, .certificate = certificate,
        .identity = certificate->rel_bytes, .sections = entering->sections, .module_id = entering->module_id,
        .owner = entering->owner, .guest = entering->guest, .bytes = entering->bytes, .capacity = entering->capacity,
        .entry_pc = canonical, .successor = WW_REL_CALLER_SUCCESSOR, .r1 = cpu->gpr[1], .r2 = cpu->gpr[2],
        .r13 = cpu->gpr[13], .r31 = cpu->gpr[31], .entry_observation_open = true};
    return NULL;
}

const char* ww_host_rel_lifecycle_boundary(WwHost* host, u32 address) {
    const char* error = host->error != NULL ? host->error : boundary(host, address);
#if WW05_ENABLE_IKADA_CONTRACT
    if (error != NULL && host->rel_ikada_admission && host->rel_pending_init.phase == WW_REL_INIT_NONE) {
        const BlueWakeRelInitContractV1* certificate = ww_host_rel_certificate(host, 60u);
        if (certificate != NULL &&
            (address & ~0x40000000u) == (certificate->lifecycle->prolog_address & ~0x40000000u) &&
            fprintf(stderr, "[rel-prolog-refusal] module=60 attempted_pc=%08X pc=%08X caller=%08X owner=%08X image_arg=%08X clause=%s\n",
                    address, host->cpu.pc, host->cpu.lr, host->cpu.gpr[31], host->cpu.gpr[3], error) < 0)
            error = "REL ikada prolog refusal diagnostic write failed";
    }
#endif
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    if (error != NULL && host->rel_actor_batch_admission && host->rel_pending_init.phase == WW_REL_INIT_NONE &&
        host->rel_contracts != NULL && host->rel_contracts->contracts != NULL &&
        host->rel_contracts->contract_count <= WW_REL_MAX_IMAGES) {
        for (u32 at = 0u; at < host->rel_contracts->contract_count; ++at) {
            const BlueWakeRelInitContractV1* certificate = &host->rel_contracts->contracts[at];
            if (ww_host_actor_batch_certificate(host, certificate->module_id) == certificate &&
                (address & ~0x40000000u) == (certificate->lifecycle->prolog_address & ~0x40000000u) &&
                fprintf(stderr, "[rel-prolog-refusal] module=%u attempted_pc=%08X pc=%08X caller=%08X owner=%08X image_arg=%08X clause=%s\n",
                        certificate->module_id, address, host->cpu.pc, host->cpu.lr,
                        host->cpu.gpr[31], host->cpu.gpr[3], error) < 0)
                error = "REL actor batch prolog refusal diagnostic write failed";
        }
    }
#endif
    if (error != NULL)
        ww_host_rel_lifecycle_revoke(host);
    return error;
}

const char* ww_host_rel_constructor_qualify(WwHost* host, const WwRelImage* image,
                                          u32 section, u32 offset, u32 caller) {
    if (image->certificate == NULL)
        return ww_host_rel_profile_constructor(host, image, section, offset, caller, false);
    WwRelPendingInit* pending = &host->rel_pending_init;
    const BlueWakeRelInitContractV1* certificate = image->certificate;
    if (pending->phase != WW_REL_INIT_ACTIVE || pending->image != image || !live_identity(host, pending) ||
        pending->constructor_completed || section != certificate->ctor_table_section ||
        offset != certificate->ctor_table_offset || caller != certificate->ctor_return_pc)
        return "REL constructor requires its matching active initialization and certified table/successor";
    pending->entry_observation_open = false;
    return NULL;
}

const char* ww_host_rel_constructor_complete(WwHost* host, const WwRelImage* image, u32 address) {
    if (image->certificate == NULL)
        return ww_host_rel_profile_constructor(host, image, 0u, 0u, address, true);
    WwRelPendingInit* pending = &host->rel_pending_init;
    if (pending->phase != WW_REL_INIT_ACTIVE || pending->image != image || !live_identity(host, pending) ||
        pending->constructor_completed || host->rel_constructor_owner != pending->owner ||
        (ww_host_rel_address(host, host->cpu.pc) & ~0x40000000u) != (address & ~0x40000000u) ||
        (address & ~0x40000000u) != (pending->certificate->ctor_return_pc & ~0x40000000u))
        return "REL constructor completion requires its matching active initialization";
    if (fprintf(stderr, "[rel-prolog] module=%u stage=ctor-completed pc=%08X owner=%08X\n",
                image->module_id, address, image->owner) < 0)
        return "REL constructor completion trace failed";
    pending->constructor_completed = true;
    pending->entry_observation_open = false;
    return NULL;
}

const char* ww_host_rel_destructor_qualify(WwHost* host, const WwRelImage* image,
                                           u32 section, u32 offset, u32 caller) {
    WwRelPendingUnlink* pending = &host->rel_pending_unlink;
    if (pending->phase != WW_REL_UNLINK_EPILOG_ACTIVE || pending->image != image ||
        !ww_host_rel_unlink_identity(host, pending) || pending->destructor_open ||
        pending->destructor_completed || image->certificate == NULL ||
        image->certificate->lifecycle == NULL || (caller & 3u) != 0u ||
        !ww_rel_translated_section(host, caller, 4u) || image->sections == NULL ||
        section >= image->section_count || (offset & 3u) != 0u ||
        (u64)offset + 4u > image->sections[section].size)
        return "REL destructor walk requires its matching admitted epilog and certified table";
    return NULL;
}

const char* ww_host_rel_destructor_complete(WwHost* host, const WwRelImage* image, u32 address) {
    WwRelPendingUnlink* pending = &host->rel_pending_unlink;
    if (pending->phase != WW_REL_UNLINK_EPILOG_ACTIVE || pending->image != image ||
        !ww_host_rel_unlink_identity(host, pending) || !pending->destructor_open ||
        pending->destructor_completed || address != pending->destructor_return ||
        (ww_host_rel_address(host, host->cpu.pc) & ~0x40000000u) != (address & ~0x40000000u))
        return "REL destructor completion requires its matching admitted epilog";
    if (fprintf(stderr, "[rel-unlink] module=%u stage=dtor-completed pc=%08X owner=%08X\n",
                image->module_id, address, image->owner) < 0)
        return "REL destructor completion trace failed";
    return NULL;
}

static bool context_inputs_match(WwHost* host) {
    const WwRelPendingInit* pending = &host->rel_pending_init;
    return mem_read32(&host->cpu, 0x800000C0u) == pending->context &&
        mem_read32(&host->cpu, 0x80003010u) == pending->handler;
}

const char* ww_host_rel_suspend_return(WwHost* host, u32 address, u32 exception, u32 vector) {
    WwRelPendingInit* pending = &host->rel_pending_init;
    if (pending->phase == WW_REL_INIT_RETURN_SUSPENDED)
        return "REL suspended return does not support a nested interruption";
    if (pending->phase != WW_REL_INIT_RETURN_READY || address != pending->successor)
        return NULL;
    if (!ww_host_rel_return_qualified(host, address) ||
        !((exception == 0x40u && vector == 0x500u) || (exception == 0x80u && vector == 0x900u)))
        return "REL interrupt delivery requires its qualified caller return";
    const u32 context = mem_read32(&host->cpu, 0x800000C0u);
    const u32 handler = mem_read32(&host->cpu, 0x80003010u);
    if ((context & 3u) != 0u || context < GC_RAM_BASE ||
        (u64)context - GC_RAM_BASE + 0x1A0u > host->cpu.ram_size ||
        ww_host_linked_alias_overlaps(context, 0x1A0u) ||
        !ww_host_module_contains_address(host->module, handler) ||
        !ww_code_guard_copied_text_matches(&host->code_guard, &host->cpu, handler, handler, 4u))
        return "REL interrupt delivery requires a validated context and DOL handler";
    pending->exception = exception;
    pending->vector = vector;
    pending->caller_msr = host->cpu.msr & PPC_MSR_RFI_MASK;
    pending->context = context;
    pending->handler = handler;
    pending->rfi_stage = WW_REL_RFI_VECTOR;
    pending->phase = WW_REL_INIT_RETURN_SUSPENDED;
    return NULL;
}

const char* ww_host_rel_rfi_prepare(WwHost* host, u32 cia, u32 verified_vector) {
    WwRelPendingInit* pending = &host->rel_pending_init;
    CPUState* cpu = &host->cpu;
    if (pending->phase != WW_REL_INIT_RETURN_SUSPENDED)
        return "REL RFI requires a suspended return";
    if (cpu->exception != 0u || (cpu->msr & PPC_MSR_PR) != 0u || !live_identity(host, pending) ||
        !context_inputs_match(host))
        return "REL suspended RFI lost its context or live authority";
    if (pending->rfi_stage == WW_REL_RFI_VECTOR) {
        if (verified_vector != pending->vector || (cia & 3u) != 0u ||
            mem_read32(cpu, cia) != 0x4C000064u || cpu->srr0 != pending->handler ||
            cpu->gpr[4] != pending->context ||
            mem_read32(cpu, pending->context + 0x198u) != pending->successor ||
            mem_read32(cpu, pending->context + 0x19Cu) != pending->caller_msr)
            return "REL RFI requires its verified vector handoff and frozen caller context";
        pending->handoff_msr = cpu->srr1 & PPC_MSR_RFI_MASK;
        return NULL;
    }
    if (verified_vector != 0u ||
        !ww_host_module_contains_address(host->module, cia) ||
        !ww_code_guard_copied_text_matches(&host->code_guard, cpu, cia, cia, 4u) ||
        mem_read32(cpu, cia) != 0x4C000064u)
        return "REL final RFI requires byte-verified mapped DOL text";
    if ((pending->successor & 3u) != 0u || cpu->srr0 != pending->successor)
        return "REL exception restoration changed the pending caller PC";
    if ((cpu->srr1 & PPC_MSR_RFI_MASK) != pending->caller_msr)
        return "REL exception restoration changed the restorable MSR";
    if (!caller_matches(host) || !pending->constructor_completed || host->rel_constructor_owner != 0u)
        return "REL prolog return did not preserve its translated caller";
    return NULL;
}

const char* ww_host_rel_rfi_finish(WwHost* host, u32 cia, bool vector_handoff) {
    WwRelPendingInit* pending = &host->rel_pending_init;
    CPUState* cpu = &host->cpu;
    if (pending->phase != WW_REL_INIT_RETURN_SUSPENDED || cpu->exception != 0u ||
        (cpu->msr & PPC_MSR_POW) != 0u || !live_identity(host, pending) || !context_inputs_match(host))
        return "REL architectural RFI failed its suspended return";
    if (vector_handoff) {
        if (pending->rfi_stage != WW_REL_RFI_VECTOR || cpu->pc != pending->handler ||
            (cpu->msr & PPC_MSR_RFI_MASK) != pending->handoff_msr)
            return "REL architectural RFI failed its verified vector handoff";
        pending->rfi_stage = WW_REL_RFI_HANDLER;
        return NULL;
    }
    if (pending->rfi_stage != WW_REL_RFI_HANDLER || cpu->pc != pending->successor ||
        (cpu->msr & PPC_MSR_RFI_MASK) != pending->caller_msr || !caller_matches(host) ||
        !ww_code_guard_copied_text_matches(&host->code_guard, cpu, cia, cia, 4u))
        return "REL architectural RFI failed its qualified caller restoration";
    pending->phase = WW_REL_INIT_RETURN_READY;
    return NULL;
}
