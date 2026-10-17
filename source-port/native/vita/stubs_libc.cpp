// E1 (PLAN_C3.md §9): newlib declares flockfile/funlockfile in stdio.h but does not implement
// them for the Vita, and fmt's file writer uses them when they are visible (its SFINAE probe
// finds the declarations). These stubs keep the symbols defined and explicit: the skeleton's
// console path is single-threaded around stdio for now, so there is nothing to lock; a real
// lock would go here once threads share a FILE.
#include <cstdio>

extern "C" void flockfile(FILE*) {}

extern "C" void funlockfile(FILE*) {}
