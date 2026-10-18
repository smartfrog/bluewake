# P-A follow-up: the bounded slice of REL actor units whose profiles the Vita boot path links.
# The full profile list needs all 502 g_profile_* definitions; this slice takes 119 units of the
# sorted src/d/actor list, which compile for ARMv7/ILP32 against the generated stub assets. It
# fills 120 profiles: d_a_gy_ctrl defines two, while d_a_itembase and d_a_kamome2 define none at
# VERSION=2 (the latter is demo-only), so d_a_kytag05 makes the 48th new profile.
#
# The units are compiled with -ffunction-sections -fdata-sections, then reduced by
# vita/reduce_profile_slice.py to their g_profile_* data: the profile's identity (mProcName, list
# IDs, size, flags) is the unit's own, but the method-table pointers are zeroed and the actor code
# is dropped. pc::staticInitCheck reads the identity only; the code needs the game API units that
# are not linked yet (the remaining compile sweep). Do not call an actor from this slice.
#
# Built for both the Vita and the host boot executable (cos_vita_boot / cos_vita_boot_host), so
# the host run exercises the same slice.
include_guard(GLOBAL)

# The reduction is GNU binutils: objcopy --remove-relocations/--localize-symbols, readelf section
# names and ld -r --gc-sections. The Vita toolchain and the Linux host have them; Apple's ld64 and
# the MSVC toolchain do not, so those builds configure without the slice (and without the host
# boot target; the Vita target only exists in the 32-bit Vita build anyway).
if (APPLE OR WIN32)
    set(COS_VITA_PROFILE_SLICE_OBJECT "" CACHE INTERNAL "No actor profile slice on this toolchain")
    message(STATUS "cos_native: cos_vita_profile_slice skipped (GNU objcopy/readelf are needed)")
    return()
endif ()
if (NOT CMAKE_READELF)
    find_program(CMAKE_READELF readelf REQUIRED)
endif ()

set(_cos_profile_slice_names
        d_a_acorn_leaf d_a_agb d_a_agbsw0 d_a_alldie d_a_am d_a_am2 d_a_amiprop
        d_a_andsw0 d_a_andsw2 d_a_arrow d_a_arrow_iceeff d_a_arrow_lighteff
        d_a_atdoor d_a_att d_a_auction d_a_bb d_a_bdk d_a_bdkobj d_a_beam
        d_a_bflower d_a_bg d_a_bgn d_a_bgn2 d_a_bgn3
        d_a_bigelf d_a_bita d_a_bk d_a_bl d_a_bmd d_a_bmdfoot d_a_bmdhand
        d_a_bo d_a_boko d_a_bomb d_a_bomb2 d_a_boomerang d_a_boss_item d_a_bpw
        d_a_branch d_a_bridge d_a_bst d_a_btd d_a_bwd d_a_bwdg d_a_bwds
        d_a_canon d_a_cc d_a_coming2 d_a_coming3 d_a_dai d_a_dai_item
        d_a_daiocta d_a_daiocta_eye d_a_deku_item d_a_demo00 d_a_demo_dk
        d_a_demo_item d_a_demo_kmm d_a_disappear d_a_dk d_a_door10 d_a_door12
        d_a_dr d_a_dr2 d_a_dummy d_a_ep d_a_esa d_a_fallrock d_a_fallrock_tag
        d_a_fan d_a_ff d_a_fganon
        d_a_fgmahou d_a_fire d_a_floor d_a_fm d_a_ghostship d_a_gm d_a_gnd
        d_a_goal_flag d_a_grass d_a_grid d_a_gy d_a_gy_ctrl d_a_himo2 d_a_himo3
        d_a_hitobj d_a_hmlif d_a_hookshot d_a_hot_floor d_a_hys d_a_ib
        d_a_icelift d_a_ikari d_a_item d_a_jbo d_a_kaji d_a_kamome d_a_kanban
        d_a_kantera d_a_kb d_a_kddoor d_a_ki d_a_kita d_a_klft d_a_kmon d_a_kn
        d_a_knob00 d_a_kokiie d_a_komore d_a_ks d_a_kt d_a_kui d_a_kytag00
        d_a_kytag01 d_a_kytag02 d_a_kytag03 d_a_kytag04 d_a_kytag05)
list(TRANSFORM _cos_profile_slice_names PREPEND "${COS_ROOT}/src/d/actor/"
        OUTPUT_VARIABLE COS_VITA_PROFILE_SLICE_UNITS)
list(TRANSFORM COS_VITA_PROFILE_SLICE_UNITS APPEND ".cpp")
list(LENGTH COS_VITA_PROFILE_SLICE_UNITS _cos_profile_slice_unit_count)

add_library(cos_vita_profile_slice_units OBJECT EXCLUDE_FROM_ALL ${COS_VITA_PROFILE_SLICE_UNITS})
target_link_libraries(cos_vita_profile_slice_units PRIVATE cos_game_headers)
target_compile_options(cos_vita_profile_slice_units PRIVATE -ffunction-sections -fdata-sections -g0)
set_property(TARGET cos_vita_profile_slice_units PROPERTY FOLDER "cos_native")

# The actor/msg/overlap base method tables every profile points at. The profile data keeps real
# pointers to them (vita_profile_list.cpp wires the slots); these three tables are reduced to data,
# so their method functions stay in the actor/API code the slice does not link yet.
set(COS_VITA_BASE_TABLE_SYMBOLS
        g_fopAc_Method g_fopMsg_Method g_fopOvlp_Method)
string(JOIN "," COS_VITA_BASE_TABLE_SYMBOLS_ARG ${COS_VITA_BASE_TABLE_SYMBOLS})
set(_cos_base_table_units
        "${COS_ROOT}/src/f_op/f_op_actor.cpp"
        "${COS_ROOT}/src/f_op/f_op_msg.cpp"
        "${COS_ROOT}/src/f_op/f_op_overlap.cpp")
add_library(cos_vita_base_table_units OBJECT EXCLUDE_FROM_ALL ${_cos_base_table_units})
target_link_libraries(cos_vita_base_table_units PRIVATE cos_game_headers)
target_compile_options(cos_vita_base_table_units PRIVATE -ffunction-sections -fdata-sections -g0)
set_property(TARGET cos_vita_base_table_units PROPERTY FOLDER "cos_native")

# g_fpcLf_Method is the one base table with real function pointers: the minimal f_pc chain
# fpcLf_* -> fpcMtd_* / fpcDwPi_* / fpcBs_MakeOfType, with everything else localized and dropped
# by --callable. Its 260 bytes of text are the bounded path; fpcBs_Create and the layer/priority
# machinery stay out.
set(_cos_fpc_chain_units
        "${COS_ROOT}/src/f_pc/f_pc_leaf.cpp"
        "${COS_ROOT}/src/f_pc/f_pc_method.cpp"
        "${COS_ROOT}/src/f_pc/f_pc_draw_priority.cpp"
        "${COS_ROOT}/src/f_pc/f_pc_base.cpp")
add_library(cos_vita_fpc_chain_units OBJECT EXCLUDE_FROM_ALL ${_cos_fpc_chain_units})
target_link_libraries(cos_vita_fpc_chain_units PRIVATE cos_game_headers)
target_compile_options(cos_vita_fpc_chain_units PRIVATE -ffunction-sections -fdata-sections -g0)
set_property(TARGET cos_vita_fpc_chain_units PROPERTY FOLDER "cos_native")

find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(COS_VITA_PROFILE_SLICE_OBJECT "${CMAKE_BINARY_DIR}/vita_profile_slice.o")
add_custom_command(OUTPUT "${COS_VITA_PROFILE_SLICE_OBJECT}"
        COMMAND "${Python3_EXECUTABLE}" "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
            --nm "${CMAKE_NM}" --objcopy "${CMAKE_OBJCOPY}" --objdump "${CMAKE_OBJDUMP}"
            --readelf "${CMAKE_READELF}" --ld "${CMAKE_LINKER}"
            --output "${COS_VITA_PROFILE_SLICE_OBJECT}"
            $<TARGET_OBJECTS:cos_vita_profile_slice_units>
        DEPENDS cos_vita_profile_slice_units "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
        COMMAND_EXPAND_LISTS
        COMMENT "Reducing the actor profile slice to its g_profile_* data"
        VERBATIM)
add_custom_target(cos_vita_profile_slice DEPENDS "${COS_VITA_PROFILE_SLICE_OBJECT}")
set(COS_VITA_PROFILE_SLICE_OBJECT "${COS_VITA_PROFILE_SLICE_OBJECT}" CACHE INTERNAL
        "The reduced actor profile slice for the Vita boot path")

set(COS_VITA_BASE_TABLES_OBJECT "${CMAKE_BINARY_DIR}/vita_base_tables.o")
add_custom_command(OUTPUT "${COS_VITA_BASE_TABLES_OBJECT}"
        COMMAND "${Python3_EXECUTABLE}" "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
            --nm "${CMAKE_NM}" --objcopy "${CMAKE_OBJCOPY}" --objdump "${CMAKE_OBJDUMP}"
            --readelf "${CMAKE_READELF}" --ld "${CMAKE_LINKER}"
            --output "${COS_VITA_BASE_TABLES_OBJECT}"
            --symbols "${COS_VITA_BASE_TABLE_SYMBOLS_ARG}"
            $<TARGET_OBJECTS:cos_vita_base_table_units>
        DEPENDS cos_vita_base_table_units "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
        COMMAND_EXPAND_LISTS
        COMMENT "Reducing the base method tables to their data"
        VERBATIM)
add_custom_target(cos_vita_base_tables DEPENDS "${COS_VITA_BASE_TABLES_OBJECT}")
set(COS_VITA_BASE_TABLES_OBJECT "${COS_VITA_BASE_TABLES_OBJECT}" CACHE INTERNAL
        "The reduced base method tables for the Vita boot path")

set(COS_VITA_FPC_CHAIN_OBJECT "${CMAKE_BINARY_DIR}/vita_fpc_chain.o")
add_custom_command(OUTPUT "${COS_VITA_FPC_CHAIN_OBJECT}"
        COMMAND "${Python3_EXECUTABLE}" "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
            --nm "${CMAKE_NM}" --objcopy "${CMAKE_OBJCOPY}" --objdump "${CMAKE_OBJDUMP}"
            --readelf "${CMAKE_READELF}" --ld "${CMAKE_LINKER}"
            --output "${COS_VITA_FPC_CHAIN_OBJECT}"
            --callable g_fpcLf_Method --keep _Z13fpcMtd_MethodPFiPvES_
            $<TARGET_OBJECTS:cos_vita_fpc_chain_units>
        DEPENDS cos_vita_fpc_chain_units "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
        COMMAND_EXPAND_LISTS
        COMMENT "Reducing the f_pc method chain to the callable g_fpcLf_Method"
        VERBATIM)
add_custom_target(cos_vita_fpc_chain DEPENDS "${COS_VITA_FPC_CHAIN_OBJECT}")
set(COS_VITA_FPC_CHAIN_OBJECT "${COS_VITA_FPC_CHAIN_OBJECT}" CACHE INTERNAL
        "The callable g_fpcLf_Method chain for the Vita boot path")
message(STATUS "cos_native: cos_vita_profile_slice (${_cos_profile_slice_unit_count} actor units, "
        "reduced to their g_profile_* data, plus the callable g_fpcLf_Method chain and three "
        "data-only base tables)")
