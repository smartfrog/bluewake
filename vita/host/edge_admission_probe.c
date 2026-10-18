#include "edge_admission.h"

#include <assert.h>

int main(void) {
    assert(ww_edge_admit_observation(0x8022F9FCu, 0x8022F9FCu, 0u, true, false));
    assert(ww_edge_admit_observation(0x8022F9FCu, 0x8022F9FCu, 0x80230000u, true, false));
    assert(!ww_edge_admit_observation(0x8022F9FCu, 0x8022F9F8u, 0u, true, false));
    assert(!ww_edge_admit_observation(0x8022F9FCu, 0x8022F9FCu, 0u, false, false));
    assert(!ww_edge_admit_observation(0x8022F9FCu, 0x8022F9FCu, 0u, true, true));
    assert(ww_edge_admit_observation(0x80120188u, 0x80120188u, 0x80120188u, true, false));
    assert(!ww_edge_admit_observation(0x80120188u, 0x80120188u, 0x8012018Cu, true, false));
    assert(ww_edge_admit_observation(0x8015EA5Cu, 0x8015EA5Cu, 0x8003EF20u, true, false));
    assert(!ww_edge_admit_observation(0x8015EA5Cu, 0x8015EA58u, 0x8003EF20u, true, false));
    assert(!ww_edge_admit_observation(0x8015EA5Cu, 0x8015EA5Cu, 0x8003EF20u, false, false));
    assert(!ww_edge_admit_observation(0x8015EA5Cu, 0x8015EA5Cu, 0x8003EF20u, true, true));
    assert(!ww_edge_admit_observation(0x8022FA00u, 0x8022FA00u, 0u, true, false));
    assert(!ww_edge_admit_observation(0x802305E0u, 0x802305E0u, 0u, true, false));
    return 0;
}
