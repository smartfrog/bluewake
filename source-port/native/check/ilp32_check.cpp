// ILP32 assertion profile (build-f5/runtime-next/ilp32-census-c415b56e §7.1).
//
// Compiled only for 32-bit host targets (the Vita/ARMv7 build; the CMake guard is
// CMAKE_SIZEOF_VOID_P == 4). It fails loudly if an assumption the ILP32 port relies on breaks: the
// pointer and size types are 32 bits, the JKR block header is the GameCube's 0x10, and the
// offset-commented fields that hold pointers or sizes (census §2.3) sit at their GameCube offsets
// again. The unit compiles with -fno-access-control (as cos_layout_check) so offsetof can name
// private members.
#include "pc/cos_pointer_width.h"

static_assert(COS_HOST_ILP32, "cos_ilp32_check is for 32-bit host targets only");

#include <cstddef>
#include <cstdint>

#include "global.h"

static_assert(sizeof(void*) == 4, "ILP32: sizeof(void*)");
static_assert(sizeof(uintptr_t) == 4, "ILP32: sizeof(uintptr_t)");
static_assert(sizeof(intptr_t) == 4, "ILP32: sizeof(intptr_t)");
static_assert(sizeof(size_t) == 4, "ILP32: sizeof(size_t)");
static_assert(sizeof(long) == 4, "ILP32: sizeof(long)");
// ARM EABI: double alignment (confirmed with the Vita toolchain). The heaps' own 16-byte alignment
// comes from OSRoundUp, not from the allocator.
static_assert(alignof(std::max_align_t) == 8, "ILP32: alignof(max_align_t)");

#include "helpers/endian.h"
// The endian detection (census §4): ARMv7/Vita is little-endian, and the swap helpers produce the
// disc's byte order. The global little-endian assertion lives in endian.h itself.
static_assert(TARGET_LITTLE_ENDIAN == 1, "ARMv7 must be detected as little-endian");
static_assert(BSWAP64(0x0102030405060708ULL) == 0x0807060504030201ULL, "BSWAP64");
static_assert(be64(0x0102030405060708ULL) == 0x0807060504030201ULL, "be64");
static_assert(BE<u64>::swap(0x0102030405060708ULL) == 0x0807060504030201ULL, "BE<u64>::swap");

#include "helpers/offset_ptr.h"
// The offset pointer stays 4 bytes on ILP32 (a BE(s32) offset, top bit as the relocated flag), as
// on the GameCube.
static_assert(sizeof(OffsetPtr) == 4, "OffsetPtr");
static_assert(sizeof(OffsetPtrT<int>) == 4, "OffsetPtrT");

#include "JSystem/JKernel/JKRExpHeap.h"
static_assert(sizeof(JKRExpHeap::CMemBlock) == 0x10, "ILP32: JKRExpHeap::CMemBlock header");

#include "JAZelAudio/JAIZelBasic.h"
static_assert(offsetof(JAIZelBasic, field_0x00d0) == 0x00D0, "JAIZelBasic::field_0x00d0");
static_assert(offsetof(JAIZelBasic, field_0x0194) == 0x0194, "JAIZelBasic::field_0x0194");
static_assert(offsetof(JAIZelBasic, field_0x0214) == 0x0214, "JAIZelBasic::field_0x0214");
static_assert(offsetof(JAIZelBasic, field_0x1f44) == 0x1F44, "JAIZelBasic::field_0x1f44");
static_assert(offsetof(JAIZelBasic, field_0x1f48) == 0x1F48, "JAIZelBasic::field_0x1f48");
static_assert(offsetof(JAIZelBasic, field_0x2064) == 0x2064, "JAIZelBasic::field_0x2064");

#include "JSystem/J3DGraphAnimator/J3DModel.h"
static_assert(offsetof(J3DModel, mUserArea) == 0x014, "J3DModel::mUserArea");

#include "JSystem/JAudio/JASHeapCtrl.h"
static_assert(offsetof(JASystem::Kernel::TSolidHeap, field_0x0) == 0x00, "TSolidHeap::field_0x0");
static_assert(offsetof(JASystem::Kernel::TSolidHeap, field_0x4) == 0x04, "TSolidHeap::field_0x4");
static_assert(offsetof(JASystem::Kernel::TSolidHeap, field_0x10) == 0x10, "TSolidHeap::field_0x10");

#include "JSystem/JKernel/JKRAramPiece.h"
static_assert(offsetof(JKRAMCommand, mSrc) == 0x48, "JKRAMCommand::mSrc");
static_assert(offsetof(JKRAMCommand, mDst) == 0x4C, "JKRAMCommand::mDst");

#include "JSystem/JKernel/JKRCompArchive.h"
// PC layout, not the GC offset (the comment says 0x64): JKRArchive carries the PC side table
// `void** mFileData` at 0x64, 4 bytes on ILP32, so the derived field starts at 0x68 (0x70 on LP64).
static_assert(offsetof(JKRCompArchive, field_0x64) == 0x68,
              "JKRCompArchive::field_0x64 (PC layout: JKRArchive's mFileData side table)");

#include "JSystem/JUtility/JUTDirectPrint.h"
static_assert(offsetof(JUTDirectPrint, mFrameBufferSize) == 0x0C, "JUTDirectPrint::mFrameBufferSize");

#include "d/d_camera.h"
static_assert(offsetof(dCamera__EventParam, mValue) == 0x10, "dCamera__EventParam::mValue");

#include "f_op/f_op_msg_mng.h"
static_assert(offsetof(fopMsgM_msgDataProc_c, field_0x2C) == 0x02C, "fopMsgM_msgDataProc_c::field_0x2C");
static_assert(offsetof(fopMsgM_msgDataProc_c, field_0x30) == 0x030, "fopMsgM_msgDataProc_c::field_0x30");
static_assert(offsetof(fopMsgM_msgDataProc_c, field_0x34) == 0x034, "fopMsgM_msgDataProc_c::field_0x34");
static_assert(offsetof(fopMsgM_msgDataProc_c, field_0x38) == 0x038, "fopMsgM_msgDataProc_c::field_0x38");

#include "m_Do/m_Do_ext.h"
// PC layout, not the GC offset (the comment says 0x58): the class chain goes through the virtual
// base J3DMtxCalc, whose Itanium-ABI layout is 4 bytes tighter than Metrowerks' (the same struct
// on LP64 is wider from its 8-byte pointers). Runtime-only class, not disc-mapped.
static_assert(offsetof(mDoExt_MtxCalcAnmBlendTblOld, mUserArea) == 0x54,
              "mDoExt_MtxCalcAnmBlendTblOld::mUserArea (PC layout: virtual-base ABI)");

#include "JSystem/JGadget/vector.h"
static_assert(offsetof(JGadget::TVector<u32>, mCapacity) == 0x0C,
              "JGadget::TVector<u32>::mCapacity");

#include "d/d_save.h"
// The packed save regions: their 25 STATIC_ASSERT(sizeof(...)) (census §6.10) fire if a pointer or
// an 8-byte field sneaks into a region. The round-trip itself is judged by the save smoke (M10) on
// a real build.
static_assert(sizeof(dSv_save_c) == 0x778, "dSv_save_c");
static_assert(sizeof(dSv_save_c_PACKED) == 0x768, "dSv_save_c_PACKED");
static_assert(sizeof(dSv_info_c) == 0x12A0, "dSv_info_c");
