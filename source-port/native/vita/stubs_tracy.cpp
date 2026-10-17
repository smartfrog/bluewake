// E1 (PLAN_C3.md §9): profiling is out of the headless skeleton's scope, and Tracy's client does
// not know newlib ("Unsupported platform!" in its TracySystem.cpp). This empty translation unit
// backs the Tracy::TracyClient stub target (cmake/Aurora.cmake): Aurora's link line is unchanged
// and no Tracy code is compiled on this target. The host builds fetch and build Tracy as before.
