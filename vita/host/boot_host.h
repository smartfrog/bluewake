#ifndef WW05_BOOT_HOST_H
#define WW05_BOOT_HOST_H

#include "module_exports.h"
#include "module_cpu_contract.h"
#include "gx_checkpoint.h"
#include "ww_renderer.h"
#include "fifo_gather.h"
#include "code_guard.h"
#include "cycle_domain.h"
#include "ipl_sram.h"
#include "dsp_interface.h"
#include "bat_contract.h"
#include "trace_sampling.h"
#include "frame_timing.h"
#include "mem1_capture.h"
#include "dvd_completion.h"
#include "rel_service.h"
#include "rel_contract.h"
#include "rel_lifecycle.h"
#include "rel_profile_block.h"
#include "pad_input.h"
#include "gxruntime/di.h"
#include "gxruntime/exi.h"
#include "gxruntime/interrupts.h"
#include "gxruntime/si.h"
#include "gxruntime/audio_dma.h"
#include "gxruntime/vi_clock.h"

#ifdef __vita__
#define WW05_DIRECTORY "ux0:data/bluewake-ww-fixture/"
#else
#define WW05_DIRECTORY ""
#endif

typedef struct WwHost {
    CPUState cpu;
    DolGuestMemory memory;
    DolGxRecompState gx;
    WwGxCheckpoint frontier;
    WwGxStream stream;
    WwRenderer* renderer;
    WwGatherPipe gather;
    WwFifoCapture fifo_capture;
    BluewakeCycleDomain cycles;
    DolInterrupts interrupts;
    DolViClock vi_clock;
    u64 vi_retraces;
    DolDi di;
    DolExi exi;
    BluewakeIplSram sram;
    DolSiDevice si;
    WwPadRead pad_read;
    void* pad_user;
    int pad_source_result;
    char pad_error[80];
    DolAudioDma audio;
    BluewakeDvdCompletions dvd_completions;
    WwRelImage rel_images[WW_REL_MAX_IMAGES];
    u32 rel_image_count;
    u32 rel_constructor_owner;
    u32 rel_constructor_return;
    u32 rel_constructor_table;
    u32 rel_constructor_size;
    const u8* rel_constructor_bytes;
    bool rel_constructor_observation_open;
    const BlueWakeShipContractV1* ship_contract;
    BlueWakeRelInitContractV1 ship_certificate;
    BlueWakeRelLifecycleV2 ship_lifecycle;
    const BlueWakeRelInitContractSetV1* rel_contracts;
    u32 rel_sdk_head;
    u32 rel_sdk_tail;
    u32 rel_sdk_pending_owner;
    bool rel_sdk_seeded;
    bool rel_sdk_failed;
    bool rel_title_admission;
#if WW05_TESTS_ONLY
    bool rel_test_admission;
#endif
    WwRelPendingInit rel_pending_init;
    WwRelProfileBlock rel_profile_block;
    WwDspInterface dsp;
    WwBatContract bat;
    const StaticRecompModuleDesc* module;
    const BlueWakeRelData* rel_data;
    u32 rel_data_count;
    u32 l2cr;
    u32 wpar;
    u32 pi_fifo_base;
    u32 pi_fifo_end;
    u32 pi_fifo_write_pointer;
    u32 cp_fifo_base;
    u32 cp_fifo_end;
    u32 cp_fifo_write_pointer;
    u32 cp_fifo_read_pointer;
    u32 cp_fifo_high_watermark;
    u32 cp_fifo_low_watermark;
    u32 cp_fifo_distance;
    WwCodeGuard code_guard;
    const char* error;
    u32 error_address;
    u32 error_pc;
    u8 error_width;
    u64 error_value;
    u32 clock_remainder;
    u32 decrementer;
    u32 hid0;
    u32 sprg0;
    u16 cp_control;
    u16 cp_perf_select;
    u16 pe_config[5];
    u16 mi_interrupt_mask;
    u16 mi_unknown1;
    u16 mi_protection;
    u64 alias_bytes;
    u64 backing_bytes;
    u32 alias_count;
    u64 edges;
    u64 exi_transfers;
    u64 disc_reads;
    u64 next_edge_progress;
    u64 syscall_resumes;
    u32 dispatch_trace_remaining;
    bool edge_stopped;
    bool decrementer_valid;
    bool decrementer_pending;
    bool aram_owned;
    bool disc_owned;
    bool dvd_callback_active;
    bool scheduler_idle_observed;
    bool post_card_idle_observed;
    bool dsp_task_boot_observed;
    bool card_probe_observed;
    bool os_panic_reported;
    bool vi_clock_enabled;
    bool gpu_finish_committed;
    u64 gpu_finish_index;
    WwFrameTiming frame_timing;
    WwFrameStamp frame_capture;
    WwMem1Capture mem1_capture;
} WwHost;

enum { WW_DVD_CALLBACK_DELAY_CYCLES = 486000u };

static inline bool ww_host_trace_syscall(u64 resume) {
    return resume <= 1u || resume % 1000u == 0u;
}

bool ww_host_init(WwHost* host);
const char* ww_host_enable_ship_contract(WwHost* host, const BlueWakeShipContractV1* contract, const char* dol);
void ww_host_shutdown(WwHost* host);
bool ww_host_load(WwHost* host, const StaticRecompModuleDesc* module,
                   const BlueWakeRelData* data, u32 count, const char* dol);
bool ww_host_edge(void* user, CPUState* cpu, u32 address);
bool ww_host_resume_exception(WwHost* host);
int ww_host_dispatch(WwHost* host, u32 address);
u64 ww_host_end_dispatch(WwHost* host);
void ww_host_fail(WwHost* host, const char* error, u32 address, u32 pc);
bool ww_host_linked_alias_overlaps(u32 address, u32 size);
DolDiCommandResult ww_host_di_read(WwHost* host, DolDiCommand* command);
bool ww_host_dvd_edge(WwHost* host, u32 address);
bool ww_host_module_contains_address(const StaticRecompModuleDesc* module, u32 address);
void ww_host_dvd_complete(WwHost* host);
const char* ww_host_disc_ready(WwHost* host);
const char* ww_host_rel_image(WwHost* host, const char* name, u8** image, u32* bytes);
const char* ww_host_materialize_rel(WwHost* host, u32 owner, const char* name);
const char* ww_host_link_profile_list(WwHost* host, u32 object);
WwRelImage* ww_host_rel_owned(WwHost* host, u32 owner);
const StaticRecompRelModule* ww_host_rel_descriptor(WwHost* host, u32 module_id);
const char* ww_host_link_rel(WwHost* host, u32 object);
const char* ww_host_validate_rel_link(WwHost* host, const StaticRecompRelModule* rel, const u8* raw, u32 bytes);
const char* ww_host_collect_rels(WwHost* host);
u32 ww_host_rel_address(WwHost* host, u32 address);
bool ww_host_rel_icbi_allowed(WwHost* host, u32 address);
bool ww_host_rel_linked(WwHost* host, u32 module_id);
WwRelImage* ww_host_rel_live(WwHost* host, u32 module_id);
const char* ww_host_unlink_rel(WwHost* host, u32 owner);
bool ww_host_rel_code_owned(WwHost* host, u32 address);
bool ww_host_rel_sections_match(const WwRelImage* slot);
bool ww_host_rel_sections_match_post_link(const WwRelImage* slot);
const char* ww_host_observe_scheduler_idle(WwHost* host);
const char* ww_host_observe_dsp_task_boot(WwHost* host);
const char* ww_host_observe_card_probe(WwHost* host);
const char* ww_host_observe_game_stop(WwHost* host, u32 address);
const char* ww_host_panic_marker(WwHost* host, u32 address, u64 value, u8 width);
void ww_host_vi_advance(WwHost* host, u64 cycles);
void ww_gx_remember_command(WwGxStream* stream, const WwGxCheckpoint* command);
const char* ww_gx_observe_first_draw(WwHost* host);
const char* ww_host_finish_gpu(WwHost* host);
const char* ww_host_frame_present(WwHost* host, u64 presented);
const char* ww_host_frame_retrace(WwHost* host);
const char* ww_host_capture_mem1(WwHost* host);
const char* ww_host_enable_frame_capture(WwHost* host);
const char* ww_gx_stream_write(WwHost* host, u8 byte, const WwFifoOrigin* origin, u32 trailing);

#endif
