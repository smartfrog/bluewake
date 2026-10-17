// E1 (PLAN_C3.md §9): the DSP is out of the headless skeleton's scope and Dolphin's DSPHLE
// (cos_dsp_hle) is not built on this target (COS_DSP_HLE=OFF; its Dolphin sources are host-only
// for now). These stubs keep every DSP entry point explicit and loud: a caller that reaches the
// DSP gets a logged failure and a defined value, never silence.
#include "cos_dsp_hle.h"

#include <cstdio>

namespace {

void ReportOnce(const char* what) {
    static bool reported = false;
    if (!reported) {
        reported = true;
        std::fprintf(stderr,
                     "[cos_sdk] DSP HLE is not built on this target (E1 skeleton): %s returns a "
                     "defined empty value\n",
                     what);
    }
}

} // namespace

namespace cos_dsp_hle {

bool Initialize(const Host&) {
    ReportOnce("Initialize");
    return false;
}

bool IsInitialized() { return false; }

void Update() {}

std::uint16_t ReadControl() {
    ReportOnce("ReadControl");
    return 0;
}

void WriteControl(std::uint16_t) { ReportOnce("WriteControl"); }

void WriteCpuMail(std::uint32_t) { ReportOnce("WriteCpuMail"); }

std::uint32_t ReadCpuMail() {
    ReportOnce("ReadCpuMail");
    return 0;
}

std::uint16_t ReadDspMailHigh() {
    ReportOnce("ReadDspMailHigh");
    return 0;
}

std::uint16_t ReadDspMailLow() {
    ReportOnce("ReadDspMailLow");
    return 0;
}

void DumpRecentMails(const char*) {}

} // namespace cos_dsp_hle
