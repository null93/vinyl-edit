#include "config.h"

#include <stdio.h>
#include <string.h>

#include "vcc_compile.h"
#include "pattern.h"
#include "format.h"

static void print_indent(int depth) {
	int i;

	for (i = 0; i < depth; i++)
		printf("    ");
}

void fmt_emit(struct fmt_state *st, struct token *t, const char *text) {
	if (t->tok == '}')
		st->indent--;

	if (st->first) {
		st->first = 0;
	}
	else if (st->need_blank) {
		printf("\n\n");
		print_indent(st->indent);
	}
	else if (st->need_newline) {
		printf("\n");
		print_indent(st->indent);
	}
	else if (t->tok == ';' || t->tok == ')' || t->tok == '.') {
		/* no space before */
	}
	else if (st->prev_tok == '(' || st->prev_tok == '.') {
		/* no space after */
	}
	else if ((st->prev_tok == CNUM || st->prev_tok == FNUM) && t->tok == ID) {
		/* no space between number and unit suffix */
	}
	else {
		printf(" ");
	}

	st->need_newline = 0;
	st->need_blank = 0;

	if (text != NULL)
		printf("%s", text);
	else
		printf("%.*s", PF(t));

	if (t->tok == '{') {
		st->indent++;
		st->need_newline = 1;
	}
	else if (t->tok == '}') {
		st->need_newline = 1;
		if (st->indent == 0)
			st->need_blank = 1;
	}
	else if (t->tok == ';') {
		st->need_newline = 1;
		if (st->indent == 0)
			st->need_blank = 1;
	}
	else if (t->tok == CSRC) {
		st->need_newline = 1;
		if (st->indent == 0)
			st->need_blank = 1;
	}

	st->prev_tok = t->tok;
}

void fmt_emit_raw(struct fmt_state *st, const char *text) {
	if (st->first) {
		st->first = 0;
	}
	else if (st->need_blank) {
		printf("\n\n");
		print_indent(st->indent);
	}
	else if (st->need_newline) {
		printf("\n");
		print_indent(st->indent);
	}
	else {
		printf(" ");
	}
	st->need_newline = 0;
	st->need_blank = 0;
	printf("%s", text);
	st->need_newline = 1;
}

void fmt_emit_source(struct fmt_state *st, struct source *src) {
	struct token *t;

	VTAILQ_FOREACH(t, &src->src_tokens, src_list) {
		if (t->tok == EOI)
			break;
		fmt_emit(st, t, NULL);
	}
}

void fmt_emit_source_caps(struct fmt_state *st, struct source *src, struct capture *caps, int ncaps) {
	struct token *t, *skip;
	int idx;
	size_t clen;
	char buf[4096];

	VTAILQ_FOREACH(t, &src->src_tokens, src_list) {
		if (t->tok == EOI)
			break;

		/* Bare **N: three tokens *, *, digit */
		if (match_bare_capture(t, &idx, &skip)) {
			if (idx <= ncaps) {
				clen = caps[idx-1].end - caps[idx-1].start;
				snprintf(buf, sizeof(buf), "%.*s", (int)clen, caps[idx-1].start);
				fmt_emit(st, skip, buf);
			}
			t = skip;
			continue;
		}

		/* CSTR with **N inside */
		if (has_capture_ref(t)) {
			substitute_captures(t->b, (size_t)(t->e - t->b), caps, ncaps, buf, sizeof(buf));
			fmt_emit(st, t, buf);
		}
		else {
			fmt_emit(st, t, NULL);
		}
	}
}

void fmt_emit_gap_comments(struct fmt_state *st, const char *from, const char *to) {
	const char *p, *start;
	char buf[8192];
	int len;

	p = from;
	while (p < to) {
		while (p < to && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
			p++;
		if (p >= to)
			break;
		start = p;
		if (*p == '/' && p + 1 < to && p[1] == '*') {
			/* C-style comment: skip to closing */
			p += 2;
			while (p + 1 < to && !(*p == '*' && p[1] == '/'))
				p++;
			if (p + 1 < to)
				p += 2;
		}
		else if (*p == '#' || (*p == '/' && p + 1 < to && p[1] == '/')) {
			while (p < to && *p != '\n')
				p++;
		}
		else {
			/* Skip non-comment gap content */
			while (p < to && *p != '\n')
				p++;
			continue;
		}
		len = (int)(p - start);
		if (len >= (int)sizeof(buf))
			len = (int)sizeof(buf) - 1;
		memcpy(buf, start, len);
		buf[len] = '\0';
		fmt_emit_raw(st, buf);
	}
}

void emit_gap(const char *from, const char *to) {
	const char *p, *start, *label;

	p = from;
	while (p < to) {
		while (p < to && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
			p++;
		if (p >= to)
			break;
		start = p;
		if (*p == '/' && p + 1 < to && p[1] == '*') {
			/* C-style multiline comment: skip to closing */
			p += 2;
			while (p + 1 < to && !(*p == '*' && p[1] == '/'))
				p++;
			if (p + 1 < to)
				p += 2;
			label = "COMMENT";
		}
		else {
			while (p < to && *p != '\n')
				p++;
			if (*start == '#' || (*start == '/' && start + 1 < to && start[1] == '/'))
				label = "COMMENT";
			else if (*start == '$')
				label = "DIRECTIVE";
			else
				label = "UNKNOWN";
		}
		printf("%-12s %.*s\n", label, (int)(p - start), start);
	}
}
