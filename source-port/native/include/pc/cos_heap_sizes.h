#pragma once
// Heap-size policy for the native (TARGET_PC) build (decision H5, docs/NATIVE_PORT_PHASE4_6.md).
//
// The GameCube sizes assume 32-bit pointers, 0x10-byte JKR block headers and 8-byte aligned
// operator new. The port keeps them for ILP32 hosts (PS Vita, ARMv7) and scales them on LP64 hosts
// (macOS, Linux, Switch), where pointers, block headers (0x20, JKRExpHeap.h) and alignment (16)
// are larger. SwitchWaker's H5 measured the LP64 factors; the ILP32 values are the GameCube values
// with no extra margin yet ("GC value + margin to measure": measure with COS_SMOKE=heap and the
// actor sweep before trusting them, census §5).
//
// Site (file)             | GameCube value                                  | LP64 factor
// ------------------------|-------------------------------------------------|------------
// m_Do_machine.cpp        | command 0x1000, archive 0xA51400 (USA),         | x2, x2, x20
//                         | game 0x2CE800 (USA); arena = what is left       |
//                         | (LP64 instead fixes the system heap at 32 MiB)  |
// f_op_actor_mng.cpp      | estimatedHeapSize (per actor)                   | x2
// m_Do_main.cpp           | audio 0x166800                                  | x2
// d_particle.cpp          | dPa_modelControl 0x16e800                       | x2
// d_mesg.cpp              | font heap 0xa32d; message heap SELECT(0xb20c..) | x2
// d_meter.cpp             | HUD 0x2a819                                     | x2
// d_message_paper.cpp     | 2D paper 0x73EA1                                | x3
// d_auction_screen.cpp    | auction screen 0x2960                           | x3
// d_minigame_starter.cpp  | starter screen SELECT(0x1300..)                 | x3
// d_ky_thunder.cpp        | lightning model 0x4a0                           | x2
//
// JKRExpHeap.cpp sizes its threshold and blocks with sizeof(CMemBlock) (0x20 on LP64, 0x10 on
// ILP32) and JAIStreamMgr.cpp computes its buffers with sizeof(pointer): both adapt on their own.

#include "pc/cos_pointer_width.h"

#if COS_HOST_LP64
#define COS_HEAP_X2(v) ((v) * 2)
#define COS_HEAP_X3(v) ((v) * 3)
#define COS_HEAP_X20(v) ((v) * 20)
#else
#define COS_HEAP_X2(v) (v)
#define COS_HEAP_X3(v) (v)
#define COS_HEAP_X20(v) (v)
#endif

// The sites with a plain literal; the argument is the GameCube value.
#define COS_HEAP_AUDIO_SIZE COS_HEAP_X2(0x166800)
#define COS_HEAP_PARTICLE_SIZE COS_HEAP_X2(0x16e800)
#define COS_HEAP_MESG_FONT_SIZE COS_HEAP_X2(0xa32d)
#define COS_HEAP_METER_SIZE COS_HEAP_X2(0x2a819)
#define COS_HEAP_PAPER_SIZE COS_HEAP_X3(0x73EA1)
#define COS_HEAP_AUCTION_SIZE COS_HEAP_X3(0x2960)
#define COS_HEAP_THUNDER_SIZE COS_HEAP_X2(0x4a0)
