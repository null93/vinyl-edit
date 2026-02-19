#ifndef FORMAT_H
#define FORMAT_H

struct token;
struct source;
struct capture;

struct fmt_state {
	int indent;
	int need_newline;
	int need_blank;
	int first;
	unsigned prev_tok;
};

/*
 * Emit a single token through the formatter, updating state.
 */
void fmt_emit(
	struct fmt_state *,
	struct token *,
	const char *
);

/*
 * Emit raw text through the formatter (for text the lexer
 * cannot tokenize, e.g. comments).  Handles pending whitespace.
 */
void fmt_emit_raw(
	struct fmt_state *,
	const char *
);

/*
 * Emit all tokens from a source through the formatter.
 */
void fmt_emit_source(
	struct fmt_state *,
	struct source *
);

/*
 * Emit tokens from a source, substituting **N capture references.
 * Bare **N: three consecutive tokens (*, *, digit) replaced by capture.
 * Quoted "**N": **N inside CSTR token replaced by capture value.
 */
void fmt_emit_source_caps(
	struct fmt_state *,
	struct source *,
	struct capture *,
	int
);

/*
 * Emit inter-token gap comments through the formatter.
 * Preserves # comments, // comments, and C-style block comments.
 */
void fmt_emit_gap_comments(
	struct fmt_state *,
	const char *,
	const char *
);

/*
 * Print inter-token gap content (comments, $ directives) as
 * labeled lines.  Skips whitespace, emits each non-blank line.
 */
void emit_gap(
	const char *,
	const char *
);

#endif
