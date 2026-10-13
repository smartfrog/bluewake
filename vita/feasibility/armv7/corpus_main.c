#include "fixture.h"
#include <stdio.h>

unsigned dolrecomp_call_depth;
int donor_main(void);

int fixture_run(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (fixture_log_open("corpus", "00000000000000000000000000000000"))
        return 1;
    fixture_print_abi();
    int result = donor_main();
    printf("upstream_c_execute=%s (unchanged reference assertions)\n", result ? "FAIL" : "PASS");
    printf("fixture_peak_guest_backing=%u (two concurrent cpu_init RAM allocations)\n",
           2u * GC_MAIN_RAM_SIZE);
    return fixture_log_close() || result;
}
