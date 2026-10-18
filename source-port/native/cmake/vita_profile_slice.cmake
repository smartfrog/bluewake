# P-A follow-up: the first bounded slice of REL actor units whose profiles the Vita boot path
# links. The full profile list needs all 502 g_profile_* definitions; this slice takes the first
# 24 units of the sorted src/d/actor list, which compile for ARMv7/ILP32 against the generated
# stub assets.
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
        d_a_bflower d_a_bg d_a_bgn d_a_bgn2 d_a_bgn3)
list(TRANSFORM _cos_profile_slice_names PREPEND "${COS_ROOT}/src/d/actor/"
        OUTPUT_VARIABLE COS_VITA_PROFILE_SLICE_UNITS)
list(TRANSFORM COS_VITA_PROFILE_SLICE_UNITS APPEND ".cpp")

add_library(cos_vita_profile_slice_units OBJECT EXCLUDE_FROM_ALL ${COS_VITA_PROFILE_SLICE_UNITS})
target_link_libraries(cos_vita_profile_slice_units PRIVATE cos_game_headers)
target_compile_options(cos_vita_profile_slice_units PRIVATE -ffunction-sections -fdata-sections -g0)
set_property(TARGET cos_vita_profile_slice_units PROPERTY FOLDER "cos_native")

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
message(STATUS "cos_native: cos_vita_profile_slice (24 actor units, reduced to their g_profile_* data)")
