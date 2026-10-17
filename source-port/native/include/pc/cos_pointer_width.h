#pragma once
// Host pointer width for the native (TARGET_PC) build's ILP32/LP64 conditionals.
//
// The port's hosts are LP64 (macOS, Linux, Switch) and ILP32 (PS Vita, ARMv7). __SIZEOF_POINTER__
// covers GCC, Clang and clang-cl; the fallback keeps an MSVC build on the 64-bit path. The census
// (build-f5/runtime-next/ilp32-census-c415b56e) is the reference for what ILP32 changes.
#if defined(__SIZEOF_POINTER__)
#define COS_POINTER_SIZE __SIZEOF_POINTER__
#elif defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__)
#define COS_POINTER_SIZE 8
#else
#define COS_POINTER_SIZE 4
#endif

#define COS_HOST_ILP32 (COS_POINTER_SIZE == 4)
#define COS_HOST_LP64 (COS_POINTER_SIZE == 8)
