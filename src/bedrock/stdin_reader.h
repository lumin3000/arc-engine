
#ifndef STDIN_READER_H
#define STDIN_READER_H

#include <stddef.h>

int stdin_reader_init(void);

void stdin_reader_shutdown(void);

int stdin_reader_poll(char *out, size_t out_size);

int stdin_reader_is_active(void);

#endif
