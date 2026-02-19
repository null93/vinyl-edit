#ifndef PATTERN_H
#define PATTERN_H

#include <stddef.h>

#define SOI 200
#define COMMENT 201
#define MAX_CAPTURES 9
#define MULTI_WILDCARD ((struct token *)1)

struct token;
struct source;

struct capture {
	const char *start;
	const char *end;
};

/*
 * Pre-process a pattern string for safe VCL lexing:
 * - Space-separate bare * runs into individual * tokens.
 *   Star runs are parsed as: ** pairs left-to-right, remainder
 *   of 3 becomes ***, remainder of 1 is a literal *.
 * - Insert space between { and " to prevent VCL long string syntax.
 * Caller must free the returned string.
 */
char *preprocess_wildcards(
	const char *
);

/*
 * Build a pattern array from a tokenized source.
 * pat[i] is the token pointer, NULL for a ** wildcard, or
 * MULTI_WILDCARD for a *** (zero-or-more) wildcard.
 * Returns the number of pattern entries.
 */
int build_pattern(
	struct source *,
	struct token **
);

/*
 * Try to match a pattern against source tokens starting at t.
 * NULL entries in pat are ** wildcards matching exactly one token.
 * MULTI_WILDCARD entries are *** wildcards matching zero or more
 * tokens (non-greedy, depth-aware for balanced {}/()).
 * Each wildcard records a capture.
 * Returns number of source tokens consumed on match, 0 on no match.
 */
int pattern_match(
	struct token *,
	struct token **,
	int,
	struct capture *,
	int *
);

/*
 * Substitute **1..**9 capture references in token text.
 * When **N is inside a quoted string and the capture is also
 * quoted, the capture's quotes are stripped to avoid doubling.
 */
void substitute_captures(
	const char *,
	size_t,
	struct capture *,
	int,
	char *,
	size_t
);

/*
 * Re-walk pattern and matched tokens to extend *** captures to
 * include inter-token gap content (comments, whitespace).  The
 * lexer skips comments and whitespace, so pattern_match records
 * *** captures as {first_token->b, last_token->e}, missing gaps.
 * This extends captures to span from the preceding token's end
 * to the following token's start, giving raw-source-faithful output.
 */
void fixup_gap_captures(
	struct token *,
	struct token **,
	int,
	struct capture *,
	int
);

/*
 * Check if the N tokens ending at t (walking backwards) match
 * all tokens in pat.  Returns 1 if pat is NULL (no constraint).
 * Supports ** and *** wildcards from build_pattern.
 */
int tokens_match_before(
	struct token *,
	struct source *
);

/*
 * Check if the N tokens starting at t (walking forward) match
 * all tokens in pat.  Returns 1 if pat is NULL (no constraint).
 * Supports ** and *** wildcards via pattern_match.
 */
int tokens_match_after(
	struct token *,
	struct source *
);

/*
 * Try to match a pattern at token t, checking the dot-boundary
 * guard and look-behind/look-ahead constraints.
 * Returns tokens consumed (>0) on match, 0 otherwise.
 */
int try_pattern_match(
	struct token *,
	struct token *,
	struct token **,
	int,
	struct source *,
	struct source *,
	struct capture *,
	int *
);

/*
 * Check if t starts a bare **N capture reference (three tokens:
 * *, *, digit).  On match, sets *idx to the capture index (1-9)
 * and *skip to the digit token.  Returns 1 on match, 0 otherwise.
 */
int match_bare_capture(
	struct token *,
	int *,
	struct token **
);

/*
 * Check if a token's text contains a **N capture reference.
 */
int has_capture_ref(
	const struct token *
);

#endif
