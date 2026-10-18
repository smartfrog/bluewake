// The harness trace hook f_op_scene.cpp calls on TARGET_PC (COS_TRACE=scene). The Vita boot path
// has no trace channels; this keeps the real scene table linkable without the crash-handler unit.
extern "C" void pc_trace_scene(int) {}
