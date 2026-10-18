#include "return_address.h"

#include <assert.h>

int main(void) {
    assert(!ww_host_linked_thumb_return((const void*)(uintptr_t)0xCCCCCCCCu));
    assert(!ww_host_linked_thumb_return((const void*)(uintptr_t)0u));
    assert(!ww_host_linked_thumb_return((const void*)(uintptr_t)1u));
    assert(!ww_host_linked_thumb_return((const void*)(uintptr_t)0x80FFFFFDu));
    assert(!ww_host_linked_thumb_return((const void*)(uintptr_t)0x81000000u));
    assert(ww_host_linked_thumb_return((const void*)(uintptr_t)0x81000001u));
    assert(ww_host_linked_thumb_return((const void*)(uintptr_t)0x81002881u));
    assert(ww_host_linked_thumb_return((const void*)(uintptr_t)0x98F315D7u));
    assert(!ww_host_linked_thumb_return((const void*)(uintptr_t)0x98F315D8u));
    return 0;
}
