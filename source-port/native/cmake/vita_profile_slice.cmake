# P-A follow-up: the bounded slice of REL actor units whose profiles the Vita boot path links.
# The full profile list needs all 502 g_profile_* definitions. This slice takes every
# profile-bearing actor unit (437) plus the 38 non-actor REL units that provide the other 50
# profiles: 475 units, 502 profiles. The actor units compile for ARMv7/ILP32 against the generated
# stub assets; d_a_gy_ctrl, d_a_npc_bj1 (9), d_a_npc_bm1 (5), d_a_npc_ko1 (2) and d_a_npc_ym1 (2)
# define several actor profiles each, while d_a_itembase, d_a_kamome2, d_a_player and
# d_a_player_npc define none at VERSION=2 (kamome2 is demo-only). d_a_npc_zl1 is back in the slice
# after fixing its Vec-array cast. The overlap and msg base tables are real; the scene, camera and
# kankyo profiles keep their identity data with zero base pointers.
#
# The units are compiled with -ffunction-sections -fdata-sections, then reduced by
# vita/reduce_profile_slice.py to their g_profile_* data: the profile's identity (mProcName, list
# IDs, size, flags) is the unit's own, and the method-table pointers are zeroed. The actor base
# table and its fopAc_* wrappers are real, but the game services they call are bounded stubs
# (vita/actor_services.cpp); pc::staticInitCheck reads the identity only.
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
        d_a_kytag01 d_a_kytag02 d_a_kytag03 d_a_kytag04 d_a_kytag05
        d_a_kytag06 d_a_kytag07 d_a_lamp d_a_lbridge d_a_leaflift d_a_lod_bg
        d_a_lstair d_a_lwood d_a_machine d_a_magma d_a_majuu_flag d_a_mant
        d_a_mbdoor d_a_mdoor d_a_mflft d_a_mgameboard d_a_mmusic d_a_mo2
        d_a_movie_player d_a_mozo d_a_msw d_a_mt d_a_mtoge d_a_nh d_a_npc_ac1
        d_a_npc_ah d_a_npc_aj1 d_a_npc_auction d_a_npc_ba1 d_a_npc_bj1
        d_a_npc_bm1 d_a_npc_bmcon1 d_a_npc_bms1 d_a_npc_bmsw d_a_npc_bs1
        d_a_npc_btsw
        d_a_npc_btsw2 d_a_npc_cb1 d_a_npc_co1 d_a_npc_de1 d_a_npc_ds1
        d_a_npc_fa1 d_a_npc_gk1 d_a_npc_gp1 d_a_npc_hi1 d_a_npc_ho d_a_npc_hr
        d_a_npc_jb1 d_a_npc_ji1 d_a_npc_kamome d_a_npc_kf1 d_a_npc_kg1
        d_a_npc_kg2 d_a_npc_kk1 d_a_npc_km1 d_a_npc_ko1 d_a_npc_kp1
        d_a_npc_ls1 d_a_npc_md d_a_npc_mk d_a_npc_mn d_a_npc_mt d_a_npc_nz
        d_a_npc_ob1 d_a_npc_os d_a_npc_p1 d_a_npc_p2 d_a_npc_people
        d_a_npc_pf1 d_a_npc_photo d_a_npc_pm1 d_a_npc_roten d_a_npc_rsh1
        d_a_npc_sarace d_a_npc_so d_a_npc_sv d_a_npc_tc d_a_npc_tt d_a_npc_uk
        d_a_npc_ym1 d_a_npc_yw1 d_a_npc_zk1 d_a_npc_zl1
        d_a_nz d_a_nzg d_a_obj_AjavW d_a_obj_Itnak d_a_obj_Vds
        d_a_obj_Vteng d_a_obj_YLzou d_a_obj_Yboil d_a_obj_Ygush00
        d_a_obj_adnno d_a_obj_ajav d_a_obj_akabe d_a_obj_apzl d_a_obj_ashut
        d_a_obj_auzu d_a_obj_aygr d_a_obj_balancelift d_a_obj_barrel
        d_a_obj_barrel2 d_a_obj_barrier d_a_obj_bemos d_a_obj_bscurtain
        d_a_obj_buoyflag d_a_obj_buoyrace d_a_obj_cafelmp d_a_obj_canon
        d_a_obj_coming d_a_obj_correct d_a_obj_demo_barrel d_a_obj_dmgroom
        d_a_obj_doguu d_a_obj_doguu_demo d_a_obj_dragonhead d_a_obj_drift
        d_a_obj_eayogn d_a_obj_ebomzo d_a_obj_eff d_a_obj_ekskz d_a_obj_eskban
        d_a_obj_ferris d_a_obj_figure d_a_obj_firewall d_a_obj_flame
        d_a_obj_ftree d_a_obj_ganonbed d_a_obj_gaship d_a_obj_gaship2
        d_a_obj_gnnbtltaki d_a_obj_gnndemotakie d_a_obj_gnndemotakis
        d_a_obj_gong d_a_obj_gryw00 d_a_obj_gtaki d_a_obj_hami2 d_a_obj_hami3
        d_a_obj_hami4 d_a_obj_hat d_a_obj_hbrf1 d_a_obj_hcbh d_a_obj_hfuck1
        d_a_obj_hha d_a_obj_hlift d_a_obj_hole d_a_obj_homen d_a_obj_homensmoke
        d_a_obj_hsehi1 d_a_obj_htetu1 d_a_obj_ice d_a_obj_iceisland
        d_a_obj_ikada d_a_obj_jump d_a_obj_kanat d_a_obj_kanoke d_a_obj_ladder
        d_a_obj_leaves d_a_obj_light d_a_obj_lpalm d_a_obj_magmarock
        d_a_obj_majyuu_door d_a_obj_mkie d_a_obj_mkiek d_a_obj_mknjd
        d_a_obj_mmrr d_a_obj_monument d_a_obj_movebox d_a_obj_msdan
        d_a_obj_msdan2 d_a_obj_msdan_sub d_a_obj_msdan_sub2 d_a_obj_mshokki
        d_a_obj_mtest d_a_obj_nest d_a_obj_ohatch d_a_obj_ojtree d_a_obj_ospbox
        d_a_obj_otble d_a_obj_paper d_a_obj_pbco d_a_obj_pbka d_a_obj_pfall
        d_a_obj_pirateship d_a_obj_plant d_a_obj_quake d_a_obj_rcloud
        d_a_obj_rflw d_a_obj_rforce d_a_obj_roten d_a_obj_search d_a_obj_shelf
        d_a_obj_shmrgrd d_a_obj_smplbg d_a_obj_stair d_a_obj_swflat
        d_a_obj_swhammer d_a_obj_swheavy d_a_obj_swlight d_a_obj_swpush
        d_a_obj_table d_a_obj_tapestry d_a_obj_tenmado d_a_obj_tide
        d_a_obj_timer d_a_obj_tntrap d_a_obj_toripost d_a_obj_tousekiki
        d_a_obj_tower d_a_obj_trap d_a_obj_tribox d_a_obj_try d_a_obj_usovmc
        d_a_obj_vfan d_a_obj_vgnfd d_a_obj_vmc d_a_obj_vmsdz d_a_obj_vmsms
        d_a_obj_volcano d_a_obj_vtil d_a_obj_vyasi d_a_obj_warpt d_a_obj_wood
        d_a_obj_xfuta d_a_obj_zouK d_a_oq
        d_a_oship d_a_pedestal d_a_ph d_a_pirate_flag d_a_player_main
        d_a_pt d_a_pw d_a_pz d_a_race_item d_a_rd d_a_rectangle d_a_sail
        d_a_saku d_a_salvage d_a_salvage_tbox d_a_sbox d_a_scene_change
        d_a_sea d_a_seatag d_a_shand d_a_ship d_a_shop_item d_a_shutter
        d_a_shutter2 d_a_sie_flag d_a_sitem d_a_sk d_a_sk2 d_a_spc_item01
        d_a_spotbox d_a_ss d_a_ssk d_a_sss d_a_st d_a_steam_tag d_a_stone
        d_a_stone2 d_a_swattack d_a_swc00 d_a_swhit0 d_a_switem
        d_a_swpropeller d_a_swtact d_a_swtdoor d_a_syan d_a_tag_attention
        d_a_tag_ba1 d_a_tag_etc
        d_a_tag_event d_a_tag_evsw d_a_tag_ghostship d_a_tag_hint d_a_tag_island
        d_a_tag_kb_item d_a_tag_kf1 d_a_tag_kk1 d_a_tag_light d_a_tag_md_cb
        d_a_tag_mk d_a_tag_msg d_a_tag_photo d_a_tag_ret d_a_tag_so
        d_a_tag_volcano d_a_tag_waterlevel d_a_tama d_a_tbox d_a_title d_a_tn
        d_a_toge d_a_tori_flag d_a_tornado d_a_tpota d_a_tsubo d_a_vrbox
        d_a_vrbox2 d_a_wall d_a_warpdm20 d_a_warpf d_a_warpfout d_a_warpgn
        d_a_warphr d_a_warpls d_a_warpmj d_a_waterfall d_a_wbird d_a_wind_tag
        d_a_windmill d_a_wz d_a_ygcwp d_a_ykgr d_a_yougan)
list(TRANSFORM _cos_profile_slice_names PREPEND "${COS_ROOT}/src/d/actor/"
        OUTPUT_VARIABLE COS_VITA_PROFILE_SLICE_UNITS)
list(TRANSFORM COS_VITA_PROFILE_SLICE_UNITS APPEND ".cpp")

# The remaining 50 profiles are the non-actor REL units of the list: scenes, msg, overlap,
# camera, kankyo. Same reduction; the overlap and msg profiles are wired to the real
# g_fopOvlp_Method/g_fopMsg_Method below, the scene/camera/kankyo base pointers stay zero (only
# the actor base table and the callable g_fpcLf_Method/g_fopOvlp_Method/g_fopMsg_Method chain are
# linked).
set(_cos_profile_slice_d_units
        d/d_ovlp_fade.cpp d/d_s_logo.cpp d/d_s_menu.cpp d/d_s_play.cpp
        d/d_s_title.cpp d/d_s_name.cpp d/d_s_open.cpp d/d_ovlp_fade2.cpp
        d/d_ovlp_fade4.cpp d/d_s_room.cpp d/d_kankyo.cpp d/d_envse.cpp
        d/d_wind_arrow.cpp d/d_wpot_water.cpp d/d_level_se.cpp
        d/d_throwstone.cpp d/d_ky_thunder.cpp d/d_wpillar.cpp
        d/d_water_mark.cpp d/d_camera.cpp d/d_kyeff.cpp d/d_kyeff2.cpp
        d/d_message_paper.cpp d/d_mesg.cpp d/d_msg.cpp d/d_message.cpp
        d/d_menu_window.cpp d/d_menu_cloth.cpp d/d_timer.cpp
        d/d_auction_screen.cpp d/d_place_name.cpp d/d_minigame_starter.cpp
        d/d_minigame_terminater.cpp d/d_meter.cpp d/d_operate_wind.cpp
        d/d_scope.cpp d/d_picture_box.cpp d/d_gameover.cpp)
list(TRANSFORM _cos_profile_slice_d_units PREPEND "${COS_ROOT}/src/")
list(APPEND COS_VITA_PROFILE_SLICE_UNITS ${_cos_profile_slice_d_units})
list(LENGTH COS_VITA_PROFILE_SLICE_UNITS _cos_profile_slice_unit_count)

add_library(cos_vita_profile_slice_units OBJECT EXCLUDE_FROM_ALL ${COS_VITA_PROFILE_SLICE_UNITS})
target_link_libraries(cos_vita_profile_slice_units PRIVATE cos_game_headers)
target_compile_options(cos_vita_profile_slice_units PRIVATE -ffunction-sections -fdata-sections -g0)
set_property(TARGET cos_vita_profile_slice_units PROPERTY FOLDER "cos_native")

# g_fpcLf_Method, g_fopOvlp_Method, g_fopMsg_Method and g_fopAc_Method have real function
# pointers: the minimal f_pc chain, the fopOvlp_* wrappers with cReq_*, the fopMsg_* wrappers with
# fopMsgM_GetAppend/fopDwTg_*/cTg/cTr/cLs/cNd, and the fopAc_* wrappers with fopAcTg_*. The actor
# services the wrappers call are bounded stubs (vita/actor_services.cpp), except g_dComIfG_gameInfo,
# whose real zeroed .bss object comes from d_com_inf_game.cpp. Everything else is localized and
# dropped by --callable. fpcBs_Create and the layer/priority machinery stay out; d_s_play is
# avoided by the two pause flags in vita/msg_pause.cpp.
set(_cos_fpc_chain_units
        "${COS_ROOT}/src/f_pc/f_pc_leaf.cpp"
        "${COS_ROOT}/src/f_pc/f_pc_method.cpp"
        "${COS_ROOT}/src/f_pc/f_pc_draw_priority.cpp"
        "${COS_ROOT}/src/f_pc/f_pc_base.cpp"
        "${COS_ROOT}/src/f_op/f_op_overlap.cpp"
        "${COS_ROOT}/src/SSystem/SComponent/c_request.cpp"
        "${COS_ROOT}/src/f_op/f_op_msg.cpp"
        "${COS_ROOT}/src/f_op/f_op_draw_tag.cpp"
        "${COS_ROOT}/src/f_op/f_op_msg_mng.cpp"
        "${COS_ROOT}/src/f_op/f_op_actor.cpp"
        "${COS_ROOT}/src/f_op/f_op_actor_tag.cpp"
        "${COS_ROOT}/src/d/d_com_inf_game.cpp"
        "${COS_ROOT}/src/SSystem/SComponent/c_tag.cpp"
        "${COS_ROOT}/src/SSystem/SComponent/c_list.cpp"
        "${COS_ROOT}/src/SSystem/SComponent/c_tree.cpp"
        "${COS_ROOT}/src/SSystem/SComponent/c_node.cpp"
        "${COS_NATIVE_ROOT}/vita/msg_pause.cpp"
        "${COS_NATIVE_ROOT}/vita/actor_services.cpp")
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

set(COS_VITA_FPC_CHAIN_OBJECT "${CMAKE_BINARY_DIR}/vita_fpc_chain.o")
add_custom_command(OUTPUT "${COS_VITA_FPC_CHAIN_OBJECT}"
        COMMAND "${Python3_EXECUTABLE}" "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
            --nm "${CMAKE_NM}" --objcopy "${CMAKE_OBJCOPY}" --objdump "${CMAKE_OBJDUMP}"
            --readelf "${CMAKE_READELF}" --ld "${CMAKE_LINKER}"
            --output "${COS_VITA_FPC_CHAIN_OBJECT}"
            --callable g_fpcLf_Method
            --keep "g_fopAc_Method,g_fopOvlp_Method,g_fopMsg_Method,_Z13fpcMtd_MethodPFiPvES_"
            --allow "OSPanic,_impure_ptr,abort,fprintf,memset,stderr"
            $<TARGET_OBJECTS:cos_vita_fpc_chain_units>
        DEPENDS cos_vita_fpc_chain_units "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
        COMMAND_EXPAND_LISTS
        COMMENT "Reducing the f_pc method chain to the callable g_fpcLf_Method"
        VERBATIM)
add_custom_target(cos_vita_fpc_chain DEPENDS "${COS_VITA_FPC_CHAIN_OBJECT}")
set(COS_VITA_FPC_CHAIN_OBJECT "${COS_VITA_FPC_CHAIN_OBJECT}" CACHE INTERNAL
        "The callable g_fpcLf_Method chain for the Vita boot path")
message(STATUS "cos_native: cos_vita_profile_slice (${_cos_profile_slice_unit_count} units, "
        "reduced to their g_profile_* data, plus the callable g_fpcLf_Method/g_fopOvlp_Method/"
        "g_fopMsg_Method/g_fopAc_Method chain)")
