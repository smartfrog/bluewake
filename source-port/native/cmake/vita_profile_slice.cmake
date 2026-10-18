# P-A follow-up: the bounded slice of REL actor units whose profiles the Vita boot path links.
# The full profile list needs all 502 g_profile_* definitions. This slice takes every
# profile-bearing actor unit (437) plus the 38 non-actor REL units that provide the other 50
# profiles: 475 units, 502 profiles. The actor units compile for ARMv7/ILP32 against the generated
# stub assets; d_a_gy_ctrl, d_a_npc_bj1 (9), d_a_npc_bm1 (5), d_a_npc_ko1 (2) and d_a_npc_ym1 (2)
# define several actor profiles each, while d_a_itembase, d_a_kamome2, d_a_player and
# d_a_player_npc define none at VERSION=2 (kamome2 is demo-only). d_a_npc_zl1 is back in the slice
# after fixing its Vec-array cast. The overlap, msg, kankyo and camera base tables are real; the
# 12 scene profiles keep their identity data with zero base pointers.
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
# camera, kankyo. Same reduction; the overlap, msg, kankyo and camera profiles are wired to their
# real base tables below, the 12 scene base pointers stay zero (g_fpcNd_Method/g_fopScn_Method pull
# f_pc_node/layer, hostIO and the harness trace hook).
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
# fopMsgM_GetAppend/fopDwTg_*/cTg/cTr/cLs/cNd, the fopAc_* wrappers with fopAcTg_*, the
# fopKy_*/fopCam_*/fopVw_* wrappers, and the scene chain fpcNd_Method/fopScn_Method with
# f_pc_node/layer, mDoHIO_root and the fopScnTg_* tags. The actor services the wrappers call are
# bounded stubs (vita/actor_services.cpp), except g_dComIfG_gameInfo and dKy_tevstr_init, whose
# real units are in the group. The harness pc_trace_scene hook is a no-op
# (vita/scene_services.cpp). Everything else is localized and dropped by --callable. fpcBs_Create
# and the priority machinery stay out; d_s_play is avoided by the two pause flags in
# vita/msg_pause.cpp.
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
        "${COS_ROOT}/src/f_op/f_op_actor_iter.cpp"
        "${COS_ROOT}/src/f_op/f_op_kankyo.cpp"
        "${COS_ROOT}/src/f_op/f_op_camera.cpp"
        "${COS_ROOT}/src/f_op/f_op_view.cpp"
        "${COS_ROOT}/src/d/d_com_inf_game.cpp"
        "${COS_ROOT}/src/d/d_kankyo.cpp"
        "${COS_ROOT}/src/d/d_stage.cpp"
        "${COS_ROOT}/src/d/d_meter.cpp"
        "${COS_ROOT}/src/d/d_event.cpp"
        "${COS_ROOT}/src/f_op/f_op_scene.cpp"
        "${COS_ROOT}/src/f_op/f_op_scene_tag.cpp"
        "${COS_ROOT}/src/f_pc/f_pc_node.cpp"
        "${COS_ROOT}/src/f_pc/f_pc_layer.cpp"
        "${COS_ROOT}/src/f_pc/f_pc_layer_iter.cpp"
        "${COS_ROOT}/src/f_pc/f_pc_method_tag.cpp"
        "${COS_ROOT}/src/f_pc/f_pc_method_iter.cpp"
        "${COS_ROOT}/src/f_pc/f_pc_searcher.cpp"
        "${COS_ROOT}/src/m_Do/m_Do_hostIO.cpp"
        "${COS_ROOT}/src/SSystem/SComponent/c_tag_iter.cpp"
        "${COS_ROOT}/src/SSystem/SComponent/c_tree_iter.cpp"
        "${COS_ROOT}/src/SSystem/SComponent/c_tag.cpp"
        "${COS_ROOT}/src/SSystem/SComponent/c_list.cpp"
        "${COS_ROOT}/src/SSystem/SComponent/c_list_iter.cpp"
        "${COS_ROOT}/src/SSystem/SComponent/c_tree.cpp"
        "${COS_ROOT}/src/SSystem/SComponent/c_node.cpp"
        "${COS_ROOT}/src/SSystem/SComponent/c_node_iter.cpp"
        "${COS_NATIVE_ROOT}/vita/msg_pause.cpp"
        "${COS_NATIVE_ROOT}/vita/actor_services.cpp"
        "${COS_NATIVE_ROOT}/vita/scene_services.cpp")
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
            --keep "g_fopAc_Method,g_fopOvlp_Method,g_fopMsg_Method,g_fopKy_Method,g_fopCam_Method,g_fopVw_Method,g_fpcNd_Method,g_fopScn_Method,_Z13fpcMtd_MethodPFiPvES_,g_dComIfG_gameInfo,g_fopAcTg_Queue,_Z13fopAcIt_JudgePFPvS_S_ES_,_ZN7daAgb_c8MapNoSetEhhh"
            --allow "OSPanic,_impure_ptr,abort,fprintf,memset,stderr,__aeabi_atexit,__dso_handle,_ZdlPvj,OSReport_Error,strncpy,_Z17fopAcM_DeleteHeapP10fopAc_ac_c,_Z19fopAcM_cullingCheckP10fopAc_ac_c,_ZN13dDemo_actor_c8setActorEP10fopAc_ac_c,_ZN14dDemo_object_c8getActorEh,_ZN12JUTAssertion10getSDeviceEv,_ZN12JUTAssertion10showAssertEjPKciS1_,_ZN6dMap_c21drawActorPointMiniMapEP10fopAc_ac_c"
            $<TARGET_OBJECTS:cos_vita_fpc_chain_units>
        DEPENDS cos_vita_fpc_chain_units "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
        COMMAND_EXPAND_LISTS
        COMMENT "Reducing the f_pc method chain to the callable g_fpcLf_Method"
        VERBATIM)
add_custom_target(cos_vita_fpc_chain DEPENDS "${COS_VITA_FPC_CHAIN_OBJECT}")
set(COS_VITA_FPC_CHAIN_OBJECT "${COS_VITA_FPC_CHAIN_OBJECT}" CACHE INTERNAL
        "The callable g_fpcLf_Method chain for the Vita boot path")

# f_op_actor_mng.cpp defines cullingCheck next to DeleteHeap. Reducing the unit with a second
# callable root keeps only the delete path, so cullingCheck stays a stub and no J3D enters the
# chain. mDoExt_destroySolidHeap and the JKR heap it calls are the whole dependency.
set(_cos_vita_deleteheap_units
        "${COS_ROOT}/src/f_op/f_op_actor_mng.cpp"
        "${COS_ROOT}/src/m_Do/m_Do_ext.cpp"
        "${COS_ROOT}/src/JSystem/JKernel/JKRHeap.cpp"
        "${COS_ROOT}/src/JSystem/JKernel/JKRSolidHeap.cpp")
add_library(cos_vita_deleteheap_units OBJECT EXCLUDE_FROM_ALL ${_cos_vita_deleteheap_units})
target_link_libraries(cos_vita_deleteheap_units PRIVATE cos_game_headers)
target_compile_options(cos_vita_deleteheap_units PRIVATE -ffunction-sections -fdata-sections -g0)
set_property(TARGET cos_vita_deleteheap_units PROPERTY FOLDER "cos_native")

set(COS_VITA_DELETEHEAP_OBJECT "${CMAKE_BINARY_DIR}/vita_deleteheap.o")
add_custom_command(OUTPUT "${COS_VITA_DELETEHEAP_OBJECT}"
        COMMAND "${Python3_EXECUTABLE}" "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
            --nm "${CMAKE_NM}" --objcopy "${CMAKE_OBJCOPY}" --objdump "${CMAKE_OBJDUMP}"
            --readelf "${CMAKE_READELF}" --ld "${CMAKE_LINKER}"
            --output "${COS_VITA_DELETEHEAP_OBJECT}"
            --callable _Z17fopAcM_DeleteHeapP10fopAc_ac_c
            --allow "OSPanic,_impure_ptr,abort,fprintf,memset,stderr,__aeabi_atexit,__dso_handle,_ZdlPvj,OSReport_Error,strncpy"
            $<TARGET_OBJECTS:cos_vita_deleteheap_units>
        DEPENDS cos_vita_deleteheap_units "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
        COMMAND_EXPAND_LISTS
        COMMENT "Reducing the actor DeleteHeap path to its fopAcM_DeleteHeap closure"
        VERBATIM)
add_custom_target(cos_vita_deleteheap DEPENDS "${COS_VITA_DELETEHEAP_OBJECT}")
set(COS_VITA_DELETEHEAP_OBJECT "${COS_VITA_DELETEHEAP_OBJECT}" CACHE INTERNAL
        "The callable fopAcM_DeleteHeap closure for the Vita boot path")

# cullingCheck is the other edge in f_op_actor_mng.cpp. It needs the J3D frustum clipper and the
# mDo_lib camera cache, but not DeleteHeap. A third callable root keeps those two services apart;
# g_dComIfG_gameInfo is kept global by the chain so this object reads the same game info.
set(_cos_vita_culling_units
        "${COS_ROOT}/src/f_op/f_op_actor_mng.cpp"
        "${COS_ROOT}/src/m_Do/m_Do_lib.cpp"
        "${COS_ROOT}/src/JSystem/J3DU/J3DUClipper.cpp"
        "${COS_ROOT}/src/JSystem/J3DGraphBase/J3DSys.cpp")
add_library(cos_vita_culling_units OBJECT EXCLUDE_FROM_ALL ${_cos_vita_culling_units})
target_link_libraries(cos_vita_culling_units PRIVATE cos_game_headers)
target_compile_options(cos_vita_culling_units PRIVATE -ffunction-sections -fdata-sections -g0)
set_property(TARGET cos_vita_culling_units PROPERTY FOLDER "cos_native")

set(COS_VITA_CULLING_OBJECT "${CMAKE_BINARY_DIR}/vita_culling.o")
add_custom_command(OUTPUT "${COS_VITA_CULLING_OBJECT}"
        COMMAND "${Python3_EXECUTABLE}" "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
            --nm "${CMAKE_NM}" --objcopy "${CMAKE_OBJCOPY}" --objdump "${CMAKE_OBJDUMP}"
            --readelf "${CMAKE_READELF}" --ld "${CMAKE_LINKER}"
            --output "${COS_VITA_CULLING_OBJECT}"
            --callable _Z19fopAcM_cullingCheckP10fopAc_ac_c
            --allow "OSPanic,_impure_ptr,abort,fprintf,memset,stderr,__aeabi_atexit,__dso_handle,_ZdlPvj,OSReport_Error,strncpy,g_dComIfG_gameInfo,C_MTXConcat,C_MTXMultVec,PSVECNormalize,tanf"
            $<TARGET_OBJECTS:cos_vita_culling_units>
        DEPENDS cos_vita_culling_units "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
        COMMAND_EXPAND_LISTS
        COMMENT "Reducing the actor culling path to its fopAcM_cullingCheck closure"
        VERBATIM)
add_custom_target(cos_vita_culling DEPENDS "${COS_VITA_CULLING_OBJECT}")
set(COS_VITA_CULLING_OBJECT "${COS_VITA_CULLING_OBJECT}" CACHE INTERNAL
        "The callable fopAcM_cullingCheck closure for the Vita boot path")

# The demo actor lookups use the inline fopAcM_SearchByID, which is the actor iterator over the
# chain's g_fopAcTg_Queue. Keep that queue and fopAcIt_Judge global in the chain, and reduce
# d_demo.cpp here so setActor/getActor share the real actor list instead of a second empty one.
set(_cos_vita_demo_units
        "${COS_ROOT}/src/d/d_demo.cpp"
        "${COS_ROOT}/src/f_pc/f_pc_searcher.cpp")
add_library(cos_vita_demo_units OBJECT EXCLUDE_FROM_ALL ${_cos_vita_demo_units})
target_link_libraries(cos_vita_demo_units PRIVATE cos_game_headers)
target_compile_options(cos_vita_demo_units PRIVATE -ffunction-sections -fdata-sections -g0)
set_property(TARGET cos_vita_demo_units PROPERTY FOLDER "cos_native")

set(COS_VITA_DEMO_OBJECT "${CMAKE_BINARY_DIR}/vita_demo.o")
add_custom_command(OUTPUT "${COS_VITA_DEMO_OBJECT}"
        COMMAND "${Python3_EXECUTABLE}" "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
            --nm "${CMAKE_NM}" --objcopy "${CMAKE_OBJCOPY}" --objdump "${CMAKE_OBJDUMP}"
            --readelf "${CMAKE_READELF}" --ld "${CMAKE_LINKER}"
            --output "${COS_VITA_DEMO_OBJECT}"
            --callable _ZN13dDemo_actor_c8getActorEv
            --keep "_ZN13dDemo_actor_c8setActorEP10fopAc_ac_c,_ZN14dDemo_object_c8getActorEh"
            --allow "OSPanic,_impure_ptr,abort,fprintf,memset,stderr,__aeabi_atexit,__dso_handle,_ZdlPvj,OSReport_Error,strncpy,_Z13fopAcIt_JudgePFPvS_S_ES_,g_fopAcTg_Queue"
            $<TARGET_OBJECTS:cos_vita_demo_units>
        DEPENDS cos_vita_demo_units "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
        COMMAND_EXPAND_LISTS
        COMMENT "Reducing the demo actor lookups to their setActor/getActor closure"
        VERBATIM)
add_custom_target(cos_vita_demo DEPENDS "${COS_VITA_DEMO_OBJECT}")
set(COS_VITA_DEMO_OBJECT "${COS_VITA_DEMO_OBJECT}" CACHE INTERNAL
        "The callable dDemo_actor_c setActor/getActor closure for the Vita boot path")

# JUT_ASSERT reaches getSDevice/showAssert; showAssert only needs the direct-print framebuffer text
# path, not JUTConsole, JUTDbPrint or the JUTFont graph. Keep those out of the reduction.
set(_cos_vita_assert_units
        "${COS_ROOT}/src/JSystem/JUtility/JUTAssert.cpp"
        "${COS_ROOT}/src/JSystem/JUtility/JUTDirectPrint.cpp")
add_library(cos_vita_assert_units OBJECT EXCLUDE_FROM_ALL ${_cos_vita_assert_units})
target_link_libraries(cos_vita_assert_units PRIVATE cos_game_headers)
target_compile_options(cos_vita_assert_units PRIVATE -ffunction-sections -fdata-sections -g0)
set_property(TARGET cos_vita_assert_units PROPERTY FOLDER "cos_native")

set(COS_VITA_ASSERT_OBJECT "${CMAKE_BINARY_DIR}/vita_assert.o")
add_custom_command(OUTPUT "${COS_VITA_ASSERT_OBJECT}"
        COMMAND "${Python3_EXECUTABLE}" "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
            --nm "${CMAKE_NM}" --objcopy "${CMAKE_OBJCOPY}" --objdump "${CMAKE_OBJDUMP}"
            --readelf "${CMAKE_READELF}" --ld "${CMAKE_LINKER}"
            --output "${COS_VITA_ASSERT_OBJECT}"
            --callable _ZN12JUTAssertion10getSDeviceEv
            --keep "_ZN12JUTAssertion10showAssertEjPKciS1_"
            --allow "OSPanic,_impure_ptr,abort,fprintf,memset,stderr,__aeabi_atexit,__dso_handle,_ZdlPvj,OSReport_Error,strncpy,OSReport,VISetNextFrameBuffer,VIWaitForRetrace,VIFlush,OSGetTime,OSTicksToMilliseconds,DCFlushRange,snprintf,vsnprintf"
            $<TARGET_OBJECTS:cos_vita_assert_units>
        DEPENDS cos_vita_assert_units "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
        COMMAND_EXPAND_LISTS
        COMMENT "Reducing the assertion path to getSDevice/showAssert and direct print"
        VERBATIM)
add_custom_target(cos_vita_assert DEPENDS "${COS_VITA_ASSERT_OBJECT}")
set(COS_VITA_ASSERT_OBJECT "${COS_VITA_ASSERT_OBJECT}" CACHE INTERNAL
        "The callable JUTAssertion getSDevice/showAssert closure for the Vita boot path")

# Map collector M1: drawActorPointMiniMap plus the attention lockon queries and the save-arrive
# call it uses. J2D is not in this closure; the collector only fills the point lists.
# daAgb_c::MapNoSet stays a bounded stub because dComIfGp_getAgb() is null on the current boot path.
set(_cos_vita_map_units
        "${COS_ROOT}/src/d/d_map.cpp"
        "${COS_ROOT}/src/d/d_attention.cpp"
        "${COS_ROOT}/src/d/d_save.cpp"
        "${COS_ROOT}/src/f_pc/f_pc_searcher.cpp")
add_library(cos_vita_map_units OBJECT EXCLUDE_FROM_ALL ${_cos_vita_map_units})
target_link_libraries(cos_vita_map_units PRIVATE cos_game_headers)
target_compile_options(cos_vita_map_units PRIVATE -ffunction-sections -fdata-sections -g0)
set_property(TARGET cos_vita_map_units PROPERTY FOLDER "cos_native")

set(COS_VITA_MAP_OBJECT "${CMAKE_BINARY_DIR}/vita_map.o")
add_custom_command(OUTPUT "${COS_VITA_MAP_OBJECT}"
        COMMAND "${Python3_EXECUTABLE}" "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
            --nm "${CMAKE_NM}" --objcopy "${CMAKE_OBJCOPY}" --objdump "${CMAKE_OBJDUMP}"
            --readelf "${CMAKE_READELF}" --ld "${CMAKE_LINKER}"
            --output "${COS_VITA_MAP_OBJECT}"
            --callable _ZN6dMap_c21drawActorPointMiniMapEP10fopAc_ac_c
            --keep "_ZN12dAttention_c12LockonTargetEi,_ZN12dAttention_c11LockonTruthEv"
            --allow "OSPanic,_impure_ptr,abort,fprintf,memset,stderr,__aeabi_atexit,__dso_handle,_ZdlPvj,OSReport_Error,strncpy,g_dComIfG_gameInfo,_Z13fopAcIt_JudgePFPvS_S_ES_,_ZN7daAgb_c8MapNoSetEhhh,_ZN12JUTAssertion10getSDeviceEv,_ZN12JUTAssertion10showAssertEjPKciS1_,strcmp,floor,floorf"
            $<TARGET_OBJECTS:cos_vita_map_units>
        DEPENDS cos_vita_map_units "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
        COMMAND_EXPAND_LISTS
        COMMENT "Reducing the map point collector and attention lockon closure"
        VERBATIM)
add_custom_target(cos_vita_map DEPENDS "${COS_VITA_MAP_OBJECT}")
set(COS_VITA_MAP_OBJECT "${COS_VITA_MAP_OBJECT}" CACHE INTERNAL
        "The callable dMap_c drawActorPointMiniMap closure for the Vita boot path")

# R1 host J2D harness: the map texture-quad draw is the only J2D use in d_map.cpp. Keep the
# reduction to dMap_2DT2_c::draw; the host test supplies a bounded GX/matrix/graf-port backend.
add_library(cos_vita_j2d_units OBJECT EXCLUDE_FROM_ALL
        "${COS_ROOT}/src/JSystem/J2DGraph/J2DGrafContext.cpp")
target_link_libraries(cos_vita_j2d_units PRIVATE cos_game_headers)
target_compile_options(cos_vita_j2d_units PRIVATE -ffunction-sections -fdata-sections -g0)
set_property(TARGET cos_vita_j2d_units PROPERTY FOLDER "cos_native")

set(COS_VITA_MAP_J2D_OBJECT "${CMAKE_BINARY_DIR}/vita_map_j2d.o")
add_custom_command(OUTPUT "${COS_VITA_MAP_J2D_OBJECT}"
        COMMAND "${Python3_EXECUTABLE}" "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
            --nm "${CMAKE_NM}" --objcopy "${CMAKE_OBJCOPY}" --objdump "${CMAKE_OBJDUMP}"
            --readelf "${CMAKE_READELF}" --ld "${CMAKE_LINKER}"
            --output "${COS_VITA_MAP_J2D_OBJECT}"
            --callable _ZN11dMap_2DT2_c4drawEv
            --allow "g_dComIfG_gameInfo,C_MTXIdentity,C_MTXTrans,_Z12mDoMtx_ZrotMPA4_fs,_ZN14mDoMtx_stack_c3nowE,GXSetVtxAttrFmt,GXClearVtxDesc,GXSetVtxDesc,GXLoadTexObj,GXSetNumChans,GXSetTevColor,GXSetNumTexGens,GXSetTexCoordGen2,GXSetNumTevStages,GXSetTevOrder,GXSetTevColorIn,GXSetTevColorOp,GXSetTevAlphaIn,GXSetTevAlphaOp,GXSetZCompLoc,GXSetZMode,GXSetBlendMode,GXSetAlphaCompare,GXSetFog,GXSetCullMode,GXSetDither,GXSetClipMode,GXLoadPosMtxImm,GXSetCurrentMtx,GXSetScissor,GXBegin,GXPosition2f32,GXTexCoord2f32,GXSetNumIndStages,GXSetTevDirect,GXSetTevOp,GXLoadTexMtxImm,GXSetChanCtrl,GXSetLineWidth,GXSetViewport,g_clearColor"
            $<TARGET_OBJECTS:cos_vita_map_units>
        DEPENDS cos_vita_map_units "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
        COMMAND_EXPAND_LISTS
        COMMENT "Reducing the R1 J2D map draw to dMap_2DT2_c::draw"
        VERBATIM)
add_custom_target(cos_vita_map_j2d DEPENDS "${COS_VITA_MAP_J2D_OBJECT}")
set(COS_VITA_MAP_J2D_OBJECT "${COS_VITA_MAP_J2D_OBJECT}" CACHE INTERNAL
        "The R1 host J2D map draw object")

# R2 host map draw harness: the real mapDrawAll enqueue path (with a synthetic stage-info object)
# plus the real dDlst_list_c draw drain. dMap_2DT2_c::draw stays available for the J2D leaf check.
set(COS_VITA_MAP_DRAW_OBJECT "${CMAKE_BINARY_DIR}/vita_map_draw.o")
add_custom_command(OUTPUT "${COS_VITA_MAP_DRAW_OBJECT}"
        COMMAND "${Python3_EXECUTABLE}" "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
            --nm "${CMAKE_NM}" --objcopy "${CMAKE_OBJCOPY}" --objdump "${CMAKE_OBJDUMP}"
            --readelf "${CMAKE_READELF}" --ld "${CMAKE_LINKER}"
            --output "${COS_VITA_MAP_DRAW_OBJECT}"
            --callable _ZN6dMap_c10mapDrawAllEffif
            --keep "_ZN11dMap_2DT2_c4drawEv,_ZN6dMap_c13mNowRoomInfoPE,_ZN6dMap_c13mRoomInfoCtrlE,_ZN6dMap_c12mMapDispModeE,_ZN6dMap_c15mDispPosLeftUpXE,_ZN6dMap_c15mDispPosLeftUpYE,_ZN6dMap_c10mDispSizeXE,_ZN6dMap_c10mDispSizeYE,_ZN6dMap_c6mAlphaE,_ZN6dMap_c10mCompAlphaE,_ZN6dMap_c9mFrameTexE,_ZN6dMap_c13mFrameTextureE,_ZN6dMap_c9m2DSQdrawE,_ZN6dMap_c10m2DSQdraw2E,_ZTV18dMap_2DMtMapSpcl_c,_ZTV18dMap_2DAGBScrDsp_c,_ZTV11dMap_2DSQ_c"
            --allow "g_dComIfG_gameInfo,g_HIO,g_mapHIO,g_clearColor,j3dSys,C_MTXIdentity,C_MTXTrans,_Z12mDoMtx_ZrotMPA4_fs,_ZN14mDoMtx_stack_c3nowE,dComIfGd_set2DOpa,OSPanic,_ZN12JUTAssertion10getSDeviceEv,_ZN12JUTAssertion10showAssertEjPKciS1_,_ZN12dDlst_list_c3setERPP12dDlst_base_cS3_S1_,_Z15mDoLib_cnvind16t,_Z15mDoLib_cnvind32j,_ZdlPvm,fmodf,memset,GXEnd,GXGetVtxAttrFmtv,GXLoadTlut,GXPosition3f32,GXPosition3s16,GXSetAlphaUpdate,GXSetColorUpdate,GXSetDstAlpha,GXSetVtxAttrFmtv,GXSetVtxAttrFmt,GXClearVtxDesc,GXSetVtxDesc,GXLoadTexObj,GXSetNumChans,GXSetTevColor,GXSetNumTexGens,GXSetTexCoordGen2,GXSetNumTevStages,GXSetTevOrder,GXSetTevColorIn,GXSetTevColorOp,GXSetTevAlphaIn,GXSetTevAlphaOp,GXSetZCompLoc,GXSetZMode,GXSetBlendMode,GXSetAlphaCompare,GXSetFog,GXSetCullMode,GXSetDither,GXSetClipMode,GXLoadPosMtxImm,GXSetCurrentMtx,GXSetScissor,GXBegin,GXPosition2f32,GXTexCoord2f32,GXSetNumIndStages,GXSetTevDirect,GXSetTevOp,GXLoadTexMtxImm,GXSetChanCtrl,GXSetLineWidth,GXSetViewport"
            $<TARGET_OBJECTS:cos_vita_map_units>
        DEPENDS cos_vita_map_units "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
        COMMAND_EXPAND_LISTS
        COMMENT "Reducing the R2 mapDrawAll closure"
        VERBATIM)
add_custom_target(cos_vita_map_draw DEPENDS "${COS_VITA_MAP_DRAW_OBJECT}")
set(COS_VITA_MAP_DRAW_OBJECT "${COS_VITA_MAP_DRAW_OBJECT}" CACHE INTERNAL
        "The R2 mapDrawAll draw object")

add_library(cos_vita_drawlist_units OBJECT EXCLUDE_FROM_ALL
        "${COS_ROOT}/src/d/d_drawlist.cpp")
target_link_libraries(cos_vita_drawlist_units PRIVATE cos_game_headers)
target_compile_options(cos_vita_drawlist_units PRIVATE -ffunction-sections -fdata-sections -g0)
set_property(TARGET cos_vita_drawlist_units PROPERTY FOLDER "cos_native")

set(COS_VITA_DRAWLIST_OBJECT "${CMAKE_BINARY_DIR}/vita_drawlist.o")
add_custom_command(OUTPUT "${COS_VITA_DRAWLIST_OBJECT}"
        COMMAND "${Python3_EXECUTABLE}" "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
            --nm "${CMAKE_NM}" --objcopy "${CMAKE_OBJCOPY}" --objdump "${CMAKE_OBJDUMP}"
            --readelf "${CMAKE_READELF}" --ld "${CMAKE_LINKER}"
            --output "${COS_VITA_DRAWLIST_OBJECT}"
            --callable _ZN12dDlst_list_c4drawEPP12dDlst_base_cS2_
            --keep "_ZN12dDlst_list_c3setERPP12dDlst_base_cS3_S1_"
            $<TARGET_OBJECTS:cos_vita_drawlist_units>
        DEPENDS cos_vita_drawlist_units "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
        COMMAND_EXPAND_LISTS
        COMMENT "Reducing dDlst_list_c::draw"
        VERBATIM)
add_custom_target(cos_vita_drawlist DEPENDS "${COS_VITA_DRAWLIST_OBJECT}")
set(COS_VITA_DRAWLIST_OBJECT "${COS_VITA_DRAWLIST_OBJECT}" CACHE INTERNAL
        "The R2 dDlst_list_c draw object")

# R3b camera tail: the public dCamera_c::Draw body path, not the full static camera_draw.
# The view/window/BG/audio camera tail remains the R3b boundary.
add_library(cos_vita_camera_units OBJECT EXCLUDE_FROM_ALL
        "${COS_ROOT}/src/d/d_camera.cpp")
target_link_libraries(cos_vita_camera_units PRIVATE cos_game_headers)
target_compile_options(cos_vita_camera_units PRIVATE -ffunction-sections -fdata-sections -g0)
target_compile_definitions(cos_vita_camera_units PRIVATE COS_R3B_CAMERA_DRAW=1)
set_property(TARGET cos_vita_camera_units PROPERTY FOLDER "cos_native")

set(COS_VITA_CAMERA_OBJECT "${CMAKE_BINARY_DIR}/vita_camera.o")
add_custom_command(OUTPUT "${COS_VITA_CAMERA_OBJECT}"
        COMMAND "${Python3_EXECUTABLE}" "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
            --nm "${CMAKE_NM}" --objcopy "${CMAKE_OBJCOPY}" --objdump "${CMAKE_OBJDUMP}"
            --readelf "${CMAKE_READELF}" --ld "${CMAKE_LINKER}"
            --output "${COS_VITA_CAMERA_OBJECT}"
            --callable _ZN9dCamera_c4DrawEv
            --allow "g_dComIfG_gameInfo,j3dSys,dComIfGd_set2DOpa,_ZN8cM_rnd_c4initEiii,GXSetVtxAttrFmt,GXClearVtxDesc,GXSetVtxDesc,GXSetNumChans,GXSetChanCtrl,GXSetNumTexGens,GXSetNumTevStages,GXSetTevColor,GXSetTevOrder,GXSetTevColorIn,GXSetTevColorOp,GXSetTevAlphaIn,GXSetTevAlphaOp,GXSetZMode,GXSetBlendMode,GXSetAlphaCompare,GXSetCullMode,GXSetClipMode,GXLoadPosMtxImm,GXBegin,GXPosition3f32,GXEnd,_ZN18dDlst_effectLine_c6updateER4cXyzR7GXColorttttffff,_ZN18dDlst_effectLine_c4drawEv"
            $<TARGET_OBJECTS:cos_vita_camera_units>
        DEPENDS cos_vita_camera_units "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
        COMMAND_EXPAND_LISTS
        COMMENT "Reducing the R3b dCamera_c::Draw tail"
        VERBATIM)
add_custom_target(cos_vita_camera DEPENDS "${COS_VITA_CAMERA_OBJECT}")
set(COS_VITA_CAMERA_OBJECT "${COS_VITA_CAMERA_OBJECT}" CACHE INTERNAL
        "The R3b camera body draw object")

set(COS_VITA_CAMERA_DRAW_OBJECT "${CMAKE_BINARY_DIR}/vita_camera_draw.o")
add_custom_command(OUTPUT "${COS_VITA_CAMERA_DRAW_OBJECT}"
        COMMAND "${Python3_EXECUTABLE}" "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
            --nm "${CMAKE_NM}" --objcopy "${CMAKE_OBJCOPY}" --objdump "${CMAKE_OBJDUMP}"
            --readelf "${CMAKE_READELF}" --ld "${CMAKE_LINKER}"
            --output "${COS_VITA_CAMERA_DRAW_OBJECT}"
            --callable cos_r3b_camera_draw
            --allow "g_dComIfG_gameInfo,j3dSys,dComIfGd_set2DOpa,C_MTXPerspective,C_MTXCopy,C_MTXInverse,C_MTXConcat,_Z13mDoMtx_lookAtPA4_fPK3VecS3_S3_s,_ZN14mDoMtx_stack_c3nowE,_ZN8cM_rnd_c4initEiii,_ZN18dDlst_effectLine_c6updateER4cXyzR7GXColorttttffff,_ZN18dDlst_effectLine_c4drawEv,_ZN6dMap_c4drawEffif,dComIfGp_setCurrentGrafPort,dComIfGp_getCurrentGrafPort,OSPanic,_Z16fopCamM_GetParamP12camera_class,_Z17fpcLf_GetPriorityP14leafdraw_class,_Z19fopOvlpM_IsDoingReqv,_Z21mDoMtx_concatProjViewPA4_KfS1_PA4_f,_Z21pc_fps60_camera_drawnP10view_class,_ZN11JAIZelBasic13getCameraInfoEP3VecPA4_fj,_ZN11JAIZelBasic16getCameraMapInfoEj,_ZN11JAIZelBasic18setCameraGroupInfoEh,_ZN11JAIZelBasic19setCameraPolygonPosEP3Vec,_ZN11JAIZelBasic9zel_basicE,_ZN11cBgS_GndChkC2Ev,_ZN14dDlst_window_c10setScissorEffff,_ZN15mDoMch_render_c14mRenderModeObjE,_ZN20dStage_roomControl_c7mStayNoE,_ZN4cBgS11GroundCrossEP11cBgS_GndChk,_ZN4dBgS12GetMtrlSndIdER13cBgS_PolyInfo,_ZN4dBgS13GetGrpSoundIdER13cBgS_PolyInfo,_ZN8cBgS_ChkD2Ev,_ZdlPvm,pc_camera_override"
            $<TARGET_OBJECTS:cos_vita_camera_units>
        DEPENDS cos_vita_camera_units "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
        COMMAND_EXPAND_LISTS
        COMMENT "Reducing the R3b camera_draw wrapper"
        VERBATIM)
add_custom_target(cos_vita_camera_draw DEPENDS "${COS_VITA_CAMERA_DRAW_OBJECT}")
set(COS_VITA_CAMERA_DRAW_OBJECT "${COS_VITA_CAMERA_DRAW_OBJECT}" CACHE INTERNAL
        "The R3b camera_draw wrapper object")

add_library(cos_vita_dbg_s_units OBJECT EXCLUDE_FROM_ALL
        "${COS_ROOT}/src/d/d_bg_s.cpp")
target_link_libraries(cos_vita_dbg_s_units PRIVATE cos_game_headers)
target_compile_options(cos_vita_dbg_s_units PRIVATE -ffunction-sections -fdata-sections -g0)
set_property(TARGET cos_vita_dbg_s_units PROPERTY FOLDER "cos_native")

set(COS_VITA_DBG_S_OBJECT "${CMAKE_BINARY_DIR}/vita_dbg_s.o")
add_custom_command(OUTPUT "${COS_VITA_DBG_S_OBJECT}"
        COMMAND "${Python3_EXECUTABLE}" "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
            --nm "${CMAKE_NM}" --objcopy "${CMAKE_OBJCOPY}" --objdump "${CMAKE_OBJDUMP}"
            --readelf "${CMAKE_READELF}" --ld "${CMAKE_LINKER}"
            --output "${COS_VITA_DBG_S_OBJECT}"
            --callable _ZN4dBgS12GetMtrlSndIdER13cBgS_PolyInfo
            --keep "_ZN4dBgS13GetGrpSoundIdER13cBgS_PolyInfo"
            --allow "OSPanic,_ZN12JUTAssertion10getSDeviceEv,_ZN12JUTAssertion10showAssertEjPKciS1_,_ZNK4cBgS9GetTriGrpEii,_ZNK4cBgS9GetGrpInfER13cBgS_PolyInfoi"
            $<TARGET_OBJECTS:cos_vita_dbg_s_units>
        DEPENDS cos_vita_dbg_s_units "${COS_NATIVE_ROOT}/vita/reduce_profile_slice.py"
        COMMAND_EXPAND_LISTS
        COMMENT "Reducing dBgS material-sound lookups"
        VERBATIM)
add_custom_target(cos_vita_dbg_s DEPENDS "${COS_VITA_DBG_S_OBJECT}")
set(COS_VITA_DBG_S_OBJECT "${COS_VITA_DBG_S_OBJECT}" CACHE INTERNAL
        "The R3b material-sound dBgS object")
message(STATUS "cos_native: cos_vita_profile_slice (${_cos_profile_slice_unit_count} units, "
        "reduced to their g_profile_* data, plus the callable g_fpcLf_Method/g_fopOvlp_Method/"
        "g_fopMsg_Method/g_fopAc_Method/g_fopKy_Method/g_fopCam_Method/g_fopVw_Method/"
        "g_fpcNd_Method/g_fopScn_Method chain)")
