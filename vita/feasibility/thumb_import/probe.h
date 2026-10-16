#ifndef THUMB_IMPORT_PROBE_H
#define THUMB_IMPORT_PROBE_H

#include <stdbool.h>
#include <stddef.h>

enum { PROBE_BYTES = 240 };
int probe_run(void);
int probe_control(void);
bool probe_record(const char *text, size_t bytes);
void probe_copy(void *destination, const void *source);

#endif
