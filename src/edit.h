#ifndef EDIT_H
#define EDIT_H

#include "pattern.h"

struct vcc;
struct source;
struct token;

struct match_constraint {
	const char *look_behind;
	const char *look_ahead;
	struct source *look_behind_src;
	struct source *look_ahead_src;
	int limit;
	int offset;
};

struct insert_opts {
	struct match_constraint match;
	const char *text;
	struct source *src;
};

struct replace_opts {
	struct match_constraint match;
	const char *from_value;
	const char *to_text;
	struct source *from_src;
	struct source *to_src;
	int to_raw;
};

struct extract_opts {
	struct match_constraint match;
	const char *from_value;
	const char *to_text;
	struct source *from_src;
	struct source *to_src;
	int to_raw;
	int strip_ws;
};

/*
 * Return nonzero if src has at least one real token (not just EOI).
 */
int source_has_tokens(
	struct source *
);

/*
 * Add synthetic SOI and EOI boundary tokens to a lexed source.
 * SOI is prepended; the existing EOI token's text is set to "EOI".
 */
void add_boundary_tokens(
	struct source *
);

/*
 * Check for non-whitespace content between tokens that the lexer
 * couldn't parse.  # comments and $ directives are expected gaps;
 * anything else is content the formatter would silently drop.
 * Returns 0 if clean, -1 if unknown content found.
 */
int check_unknown_gaps(
	struct source *
);

/*
 * Check if text contains bare $ or # (outside double quotes).
 * Used to detect replacement text the VCL lexer cannot tokenize.
 */
int text_needs_raw(
	const char *
);

/*
 * Lex a pattern string, pre-processing ** wildcards to prevent
 * compound operator formation (e.g. *= from **=).
 * Caller must free *preprocessed if non-NULL.
 */
void lex_pattern(
	struct vcc *,
	const char *,
	struct source **,
	char **
);

/*
 * Print the token stream for debugging.  If processed is set,
 * include SOI/EOI markers and inter-token gap content.
 */
void cmd_tokens(
	struct source *,
	int
);

/*
 * Pass 1: Walk the token stream and apply replace operations,
 * writing raw VCL text to a buffer.  The caller re-tokenizes
 * and formats the result in pass 2.
 * Returns a malloc'd null-terminated string.
 */
char *emit_transform_replace(
	struct source *,
	const struct replace_opts *
);

/*
 * Walk the token stream and emit formatted output, applying
 * insert and/or replace operations.  Pass NULL to skip.
 */
void emit_formatted(
	struct source *,
	const struct insert_opts *,
	const struct replace_opts *
);

/*
 * Walk the token stream, find pattern matches, and print each
 * match.  In 1-arg mode (no template), print the raw source text
 * of the matched region.  In 2-arg mode, substitute captures into
 * the template and print the result.
 */
void cmd_extract(
	struct source *,
	const struct extract_opts *
);

/*
 * Scan inter-token gaps for comments and insert synthetic COMMENT
 * tokens into the source's token list.  This makes comments visible
 * to pattern matching in the extract command.
 */
void add_comment_tokens(
	struct source *
);

/*
 * If a lexed pattern source has no tokens (the lexer stripped it
 * as a comment), create a COMMENT token from the source text.
 */
void make_comment_source(
	struct source *
);

#endif
