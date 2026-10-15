#ifndef WW_ACTOR_BATCH_POLICY_H
#define WW_ACTOR_BATCH_POLICY_H

static const struct {
    u32 id;
    const char* identity;
    bool actor;
} ww_actor_expected[] = {
#if WW05_TESTS_ONLY
    {60u,  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", false},
    {84u,  "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", true},
    {85u,  "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc", true},
    {168u, "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd", false},
    {400u, "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee", false}
#else
    {60u,  "8b8d43495a0bc4912b2791132ab6cbab6e37b51ed2ea4f9cfb8b02481d8ec9d2", false},
    {84u,  "d30d7d3df4301935f25cd6d0a5c75ccf9dd53e7e5798cf924f737455d6748fca", true},
    {168u, "36e7c106d89d7685627d645f2649214f68e2aec31332cc5d1c099b2ad65f5212", false},
    {400u, "1eac333820fdb3898936b59ac7cf4abcbbecdd3f5aac54b6f28b3848d60d6e54", false}
#endif
};

#if WW05_TESTS_ONLY
#define WW_ACTOR_COUNT 2u
#define WW_ACTOR_SET_IDENTITY "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
#else
#define WW_ACTOR_COUNT 1u
#define WW_ACTOR_SET_IDENTITY "badf74fc5b0d634faac04009fe77bc6798993d0d46628c3a836c7d5a21d19f28"
#endif
#define WW_ACTOR_FULL_COUNT ((u32)(sizeof(ww_actor_expected) / sizeof(ww_actor_expected[0])))

#endif
