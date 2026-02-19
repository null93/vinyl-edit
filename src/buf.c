#include <stdlib.h>
#include <string.h>

#include "buf.h"

void buf_init(struct buf *b) {
	b->cap = 4096;
	b->len = 0;
	b->data = malloc(b->cap);
}

void buf_append(struct buf *b, const char *s, size_t n) {
	while (b->len + n + 1 >= b->cap) {
		b->cap *= 2;
		b->data = realloc(b->data, b->cap);
	}
	memcpy(b->data + b->len, s, n);
	b->len += n;
}

void buf_appendc(struct buf *b, char c) {
	buf_append(b, &c, 1);
}

void buf_appends(struct buf *b, const char *s) {
	buf_append(b, s, strlen(s));
}
