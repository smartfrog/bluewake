#ifndef WW_ACTOR_BATCH_POLICY_H
#define WW_ACTOR_BATCH_POLICY_H

static const struct {
    u32 id;
    const char* identity;
    bool actor;
    WwActorSdkContract sdk_contract;
} ww_actor_expected[] = {
#if WW05_TESTS_ONLY
    {60u,  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", false, WW_ACTOR_SDK_EXISTING},
    {84u,  "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", true, WW_ACTOR_SDK_CERTIFIED_INLINE_ZERO},
    {85u,  "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc", true, WW_ACTOR_SDK_CERTIFIED_INLINE_ZERO},
    {168u, "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd", false, WW_ACTOR_SDK_EXISTING},
    {400u, "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee", false, WW_ACTOR_SDK_EXISTING}
#else
#include "bulk_actor_policy.inc"
#endif
};

#if WW05_TESTS_ONLY
#define WW_ACTOR_COUNT 2u
#define WW_ACTOR_POLICY_PIN 0xecb7de83dc2b9e50ull
#define WW_ACTOR_POLICY_IDENTITY "inline-zero-v1-fnv1a64-ecb7de83dc2b9e50"
#define WW_ACTOR_SET_IDENTITY "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
#endif
#define WW_ACTOR_FULL_COUNT ((u32)(sizeof(ww_actor_expected) / sizeof(ww_actor_expected[0])))
/* Pin encoding: BE32 revision, then sorted BE32 id/actor/contract + 64 ASCII certificate hex bytes. */
#define WW_ACTOR_POLICY_REVISION 1u

#endif
