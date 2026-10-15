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
    {9u,   "32728fa36d02fa0bb4e70bbb06a8481bcc38004d55d2828789c735c22252bf75", true, WW_ACTOR_SDK_OWNER_PUBLISHED},
    {19u,  "fe2149f8e76f6529960061997e0f01f3cf43bd8142f78a5fa879d07738cec97b", true, WW_ACTOR_SDK_OWNER_PUBLISHED},
    {60u,  "8b8d43495a0bc4912b2791132ab6cbab6e37b51ed2ea4f9cfb8b02481d8ec9d2", false, WW_ACTOR_SDK_EXISTING},
    {81u,  "fd19d0bd5ee8cda4adfda165d9b706f3f54da2c24c0780d03e322cc2e6c8c085", true, WW_ACTOR_SDK_OWNER_PUBLISHED},
    {84u,  "d30d7d3df4301935f25cd6d0a5c75ccf9dd53e7e5798cf924f737455d6748fca", true, WW_ACTOR_SDK_CERTIFIED_INLINE_ZERO},
    {168u, "36e7c106d89d7685627d645f2649214f68e2aec31332cc5d1c099b2ad65f5212", false, WW_ACTOR_SDK_EXISTING},
    {400u, "1eac333820fdb3898936b59ac7cf4abcbbecdd3f5aac54b6f28b3848d60d6e54", false, WW_ACTOR_SDK_EXISTING}
#endif
};

#if WW05_TESTS_ONLY
#define WW_ACTOR_COUNT 2u
#define WW_ACTOR_POLICY_PIN 0xecb7de83dc2b9e50ull
#define WW_ACTOR_POLICY_IDENTITY "inline-zero-v1-fnv1a64-ecb7de83dc2b9e50"
#define WW_ACTOR_SET_IDENTITY "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
#else
#define WW_ACTOR_COUNT 4u
#define WW_ACTOR_POLICY_PIN 0x29e153b982999729ull
#define WW_ACTOR_POLICY_IDENTITY "inline-zero-v1-fnv1a64-29e153b982999729"
#define WW_ACTOR_SET_IDENTITY "95885d356fa97040e96c90a142c7f9d50ede5700c61b246283c2a7a39fc00cee"
#endif
#define WW_ACTOR_FULL_COUNT ((u32)(sizeof(ww_actor_expected) / sizeof(ww_actor_expected[0])))
/* Pin encoding: BE32 revision, then sorted BE32 id/actor/contract + 64 ASCII certificate hex bytes. */
#define WW_ACTOR_POLICY_REVISION 1u

#endif
