#ifndef BUF_H
#define BUF_H

#include <stddef.h>

struct buf {
	char *data;
	size_t len;
	size_t cap;
};

/*
 * Initialize a dynamic buffer with default capacity.
 */
void buf_init(
	struct buf *
);

/*
 * Append n bytes from s to the buffer, growing if needed.
 */
void buf_append(
	struct buf *,
	const char *,
	size_t
);

/*
 * Append a single character to the buffer.
 */
void buf_appendc(
	struct buf *,
	char
);

/*
 * Append a null-terminated string to the buffer.
 */
void buf_appends(
	struct buf *,
	const char *
);

#endif
