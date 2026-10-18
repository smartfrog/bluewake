// The two pause flags f_op_msg.cpp reads through dScnPly_ply_c::isPause (fopMsg_Execute). Linking
// the real d_s_play.cpp would pull the whole PLAY scene; these flags are all the msg base table
// needs, so they are defined here instead. Values and semantics are the game's (s8, zero-initial).
#include "d/d_s_play.h"

s8 dScnPly_ply_c::pauseTimer;
s8 dScnPly_ply_c::nextPauseTimer;
