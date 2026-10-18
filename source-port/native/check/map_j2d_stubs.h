#pragma once

struct GxCounters {
    int set_num_ind_stages;
    int set_tev_direct;
    int load_pos_mtx;
    int load_tex_mtx;
    int begin;
    int pos2f;
    int tex2f;
    int set_vtx_fmt;
    int set_line_width;
    int set_scissor;
    int set_alpha_compare;
    int set_fog;
    int set_chan_ctrl;
    int set_tev_color;
};

extern GxCounters g_gx;
void reset_gx_counters();
