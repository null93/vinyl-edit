#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "vcc_compile.h"
#include "pattern.h"

static int tokens_equal(const struct token *a, const struct token *b) {
	size_t alen = a->e - a->b;
	size_t blen = b->e - b->b;

	return (alen == blen && memcmp(a->b, b->b, alen) == 0);
}

static int is_bare_star(const struct token *t) {
	return ((size_t)(t->e - t->b) == 1 && t->b[0] == '*');
}

static int capture_digit(const struct token *t) {
	size_t len = t->e - t->b;

	if (len == 1 && t->b[0] >= '1' && t->b[0] <= '9')
		return (t->b[0] - '0');
	return (0);
}

int has_capture_ref(const struct token *t) {
	const char *p;

	for (p = t->b; p < t->e - 2; p++) {
		if (*p == '*' && p[1] == '*' && p[2] >= '1' && p[2] <= '9')
			return (1);
	}
	return (0);
}

int match_bare_capture(struct token *t, int *idx, struct token **skip) {
	struct token *t2, *t3;
	int d;

	if (!is_bare_star(t))
		return (0);
	t2 = VTAILQ_NEXT(t, src_list);
	if (t2 == NULL || t2->tok == EOI || !is_bare_star(t2))
		return (0);
	t3 = VTAILQ_NEXT(t2, src_list);
	if (t3 == NULL || t3->tok == EOI)
		return (0);
	d = capture_digit(t3);
	if (d == 0)
		return (0);
	*idx = d;
	*skip = t3;
	return (1);
}

char *preprocess_wildcards(const char *text) {
	size_t len, i, oi, count, g;
	int in_str, pairs, has_triple;
	char *out;

	len = strlen(text);
	out = malloc(len * 3 + 1);
	if (out == NULL)
		return (NULL);
	oi = 0;
	in_str = 0;
	for (i = 0; i < len; i++) {
		if (text[i] == '"')
			in_str = !in_str;
		if (!in_str && text[i] == '*') {
			/* Count consecutive stars */
			count = 0;
			while (i + count < len && text[i + count] == '*')
				count++;
			if (count == 1) {
				out[oi++] = '*';
				continue;
			}
			/* ** pairs first, *** if odd remainder */
			if (count % 2 == 1 && count >= 3) {
				has_triple = 1;
				pairs = (count - 3) / 2;
			}
			else {
				has_triple = 0;
				pairs = count / 2;
			}
			if (oi > 0 && out[oi - 1] != ' ' && out[oi - 1] != '\t')
				out[oi++] = ' ';
			for (g = 0; g < (size_t)pairs; g++) {
				if (g > 0)
					out[oi++] = ' ';
				out[oi++] = '*';
				out[oi++] = ' ';
				out[oi++] = '*';
			}
			if (has_triple) {
				if (pairs > 0)
					out[oi++] = ' ';
				out[oi++] = '*';
				out[oi++] = ' ';
				out[oi++] = '*';
				out[oi++] = ' ';
				out[oi++] = '*';
			}
			if (i + count < len && text[i + count] != ' ' && text[i + count] != '\t')
				out[oi++] = ' ';
			i += count - 1;
			continue;
		}
		/* Prevent {"..."} long string syntax */
		if (!in_str && text[i] == '{' && i + 1 < len && text[i + 1] == '"') {
			out[oi++] = '{';
			out[oi++] = ' ';
			continue;
		}
		out[oi++] = text[i];
	}
	out[oi] = '\0';
	return (out);
}

int build_pattern(struct source *src, struct token **pat) {
	struct token *t, *scan;
	int n, star_count, pairs, has_triple, g;

	n = 0;
	for (t = VTAILQ_FIRST(&src->src_tokens); t != NULL; t = VTAILQ_NEXT(t, src_list)) {
		if (t->tok == EOI)
			break;
		if (is_bare_star(t)) {
			star_count = 1;
			scan = VTAILQ_NEXT(t, src_list);
			while (scan != NULL && scan->tok != EOI && is_bare_star(scan)) {
				star_count++;
				scan = VTAILQ_NEXT(scan, src_list);
			}
			if (star_count >= 3 && star_count % 2 == 1) {
				has_triple = 1;
				pairs = (star_count - 3) / 2;
			}
			else {
				has_triple = 0;
				pairs = star_count / 2;
			}
			for (g = 0; g < pairs; g++)
				pat[n++] = NULL;
			if (has_triple)
				pat[n++] = MULTI_WILDCARD;
			if (star_count == 1)
				pat[n++] = t;
			/* Advance t to last star token */
			for (g = 1; g < star_count; g++)
				t = VTAILQ_NEXT(t, src_list);
			continue;
		}
		pat[n++] = t;
	}
	return (n);
}

int pattern_match(struct token *t, struct token **pat, int npat, struct capture *caps, int *ncaps) {
	struct token *cur, *try_cur;
	struct capture rest_caps[MAX_CAPTURES];
	const char *cap_start, *cap_end;
	int consumed, i, depth, extra, rest_ncaps, rest_matched;
	int saved_ncaps, k;

	cur = t;
	consumed = 0;
	*ncaps = 0;
	for (i = 0; i < npat; i++) {
		if (pat[i] == MULTI_WILDCARD) {
			saved_ncaps = *ncaps;
			if (i + 1 >= npat) {
				/* Last in pattern: match to EOI */
				cap_start = NULL;
				cap_end = NULL;
				extra = 0;
				if (cur != NULL && cur->tok != EOI)
					cap_start = cur->b;
				while (cur != NULL && cur->tok != EOI) {
					cap_end = cur->e;
					cur = VTAILQ_NEXT(cur, src_list);
					extra++;
				}
				caps[*ncaps].start = cap_start;
				caps[*ncaps].end = cap_end;
				(*ncaps)++;
				consumed += extra;
				break;
			}
			/* Non-greedy: try 0, 1, 2, ... tokens */
			cap_start = NULL;
			cap_end = NULL;
			depth = 0;
			try_cur = cur;
			extra = 0;
			for (;;) {
				if (depth == 0) {
					rest_ncaps = 0;
					rest_matched = pattern_match(try_cur, pat + i + 1,
					    npat - i - 1, rest_caps, &rest_ncaps);
					if (rest_matched > 0) {
						caps[saved_ncaps].start = cap_start;
						caps[saved_ncaps].end = cap_end;
						*ncaps = saved_ncaps + 1;
						for (k = 0; k < rest_ncaps; k++)
							caps[(*ncaps)++] = rest_caps[k];
						consumed += extra + rest_matched;
						return (consumed);
					}
				}
				if (try_cur == NULL || try_cur->tok == EOI || try_cur->tok == SOI)
					return (0);
				if (cap_start == NULL)
					cap_start = try_cur->b;
				cap_end = try_cur->e;
				if (try_cur->tok == '{' || try_cur->tok == '(')
					depth++;
				if (try_cur->tok == '}' || try_cur->tok == ')') {
					depth--;
					if (depth < 0)
						return (0);
				}
				try_cur = VTAILQ_NEXT(try_cur, src_list);
				extra++;
			}
		}
		else if (pat[i] == NULL) {
			/* Single wildcard: match exactly one token */
			if (cur == NULL || cur->tok == EOI || cur->tok == SOI)
				return (0);
			caps[*ncaps].start = cur->b;
			caps[*ncaps].end = cur->e;
			(*ncaps)++;
			cur = VTAILQ_NEXT(cur, src_list);
			consumed++;
		}
		else {
			if (cur == NULL)
				return (0);
			if (!tokens_equal(cur, pat[i]))
				return (0);
			cur = VTAILQ_NEXT(cur, src_list);
			consumed++;
		}
	}
	return (consumed);
}

void substitute_captures(
    const char *text, size_t len, struct capture *caps, int ncaps,
    char *out, size_t outsz) {
	size_t oi, i, j;
	int idx, in_str, cap_quoted;
	size_t clen;
	const char *cb;

	oi = 0;
	in_str = (len >= 2 && text[0] == '"');
	for (i = 0; i < len && oi < outsz - 1; i++) {
		if (text[i] == '*' && i + 1 < len && text[i + 1] == '*' &&
		    i + 2 < len && text[i + 2] >= '1' && text[i + 2] <= '9') {
			idx = text[i + 2] - '1';
			if (idx < ncaps && caps[idx].start != NULL) {
				cb = caps[idx].start;
				clen = caps[idx].end - cb;
				cap_quoted = (clen >= 2 && cb[0] == '"' && cb[clen - 1] == '"');
				if (in_str && cap_quoted) {
					cb++;
					clen -= 2;
				}
				for (j = 0; j < clen && oi < outsz - 1; j++)
					out[oi++] = cb[j];
			}
			i += 2;
			continue;
		}
		out[oi++] = text[i];
	}
	out[oi] = '\0';
}

void fixup_gap_captures(
    struct token *start, struct token **pat, int npat,
    struct capture *caps, int ncaps) {
	struct token *cur;
	const char *prev_e;
	int ci, pi;

	cur = start;
	ci = 0;
	prev_e = NULL;

	for (pi = 0; pi < npat && ci < ncaps; pi++) {
		if (pat[pi] == MULTI_WILDCARD) {
			if (caps[ci].start == NULL) {
				/* Zero tokens matched -- capture the gap */
				if (prev_e != NULL) {
					caps[ci].start = prev_e;
					caps[ci].end = (cur != NULL &&
					    cur->tok != EOI) ?
					    cur->b : prev_e;
				}
			}
			else {
				/* N tokens matched -- extend to gap bounds */
				if (prev_e != NULL)
					caps[ci].start = prev_e;
				while (cur != NULL && cur->tok != EOI) {
					if (cur->e >= caps[ci].end) {
						prev_e = cur->e;
						cur = VTAILQ_NEXT(cur,
						    src_list);
						break;
					}
					cur = VTAILQ_NEXT(cur, src_list);
				}
				if (cur != NULL && cur->tok != EOI)
					caps[ci].end = cur->b;
			}
			ci++;
		}
		else if (pat[pi] == NULL) {
			if (cur != NULL && cur->tok != EOI) {
				prev_e = cur->e;
				cur = VTAILQ_NEXT(cur, src_list);
			}
			ci++;
		}
		else {
			if (cur != NULL && cur->tok != EOI) {
				prev_e = cur->e;
				cur = VTAILQ_NEXT(cur, src_list);
			}
		}
	}
}

int tokens_match_before(struct token *t, struct source *pat) {
	struct token *arr[128];
	struct capture caps[MAX_CAPTURES];
	struct token *cur, *last;
	int n, i, ncaps, matched, has_multi;

	if (pat == NULL)
		return (1);

	n = build_pattern(pat, arr);
	if (n == 0)
		return (1);

	/* Check if pattern has MULTI_WILDCARD */
	has_multi = 0;
	for (i = 0; i < n; i++) {
		if (arr[i] == MULTI_WILDCARD) {
			has_multi = 1;
			break;
		}
	}

	if (!has_multi) {
		/* Simple backward check */
		cur = t;
		for (i = n - 1; i >= 0; i--) {
			if (cur == NULL)
				return (0);
			if (arr[i] == NULL) {
				/* ** wildcard: skip boundary tokens */
				if (cur->tok == SOI)
					return (0);
			}
			else if (!tokens_equal(cur, arr[i]))
				return (0);
			cur = VTAILQ_PREV(cur, tokenhead, src_list);
		}
		return (1);
	}

	/* Walk backward, trying pattern_match from each position.
	 * A match is valid if the last consumed token reaches t.
	 * For patterns ending with ***, the match may extend past t
	 * (*** consumes to EOI), so accept last >= t.
	 */
	cur = t;
	for (i = 0; i < 256 && cur != NULL; i++) {
		ncaps = 0;
		matched = pattern_match(cur, arr, n, caps, &ncaps);
		if (matched > 0) {
			last = cur;
			for (int j = 1; j < matched; j++)
				last = VTAILQ_NEXT(last, src_list);
			if (last == t)
				return (1);
			/* Pattern ends with *** and consumed past t */
			if (arr[n - 1] == MULTI_WILDCARD && last != NULL &&
			    last->b >= t->b)
				return (1);
		}
		cur = VTAILQ_PREV(cur, tokenhead, src_list);
	}
	return (0);
}

int tokens_match_after(struct token *t, struct source *pat) {
	struct token *arr[128];
	struct capture caps[MAX_CAPTURES];
	int n, ncaps;

	if (pat == NULL)
		return (1);

	n = build_pattern(pat, arr);
	if (n == 0)
		return (1);

	ncaps = 0;
	return (pattern_match(t, arr, n, caps, &ncaps) > 0);
}

int try_pattern_match(
    struct token *t, struct token *prev,
    struct token **from_pat, int from_npat,
    struct source *look_behind_src, struct source *look_ahead_src,
    struct capture *caps, int *ncaps) {
	struct token *after;
	int matched, i;

	/* Dot-boundary guard */
	if (from_pat[0] != NULL && from_pat[0] != MULTI_WILDCARD &&
		from_pat[0]->tok == '.' && prev != NULL &&
		prev->tok != '{' && prev->tok != ';')
		return (0);

	matched = pattern_match(t, from_pat, from_npat, caps, ncaps);
	if (matched <= 0)
		return (0);

	if (!tokens_match_before(prev, look_behind_src))
		return (0);

	after = t;
	for (i = 0; i < matched; i++)
		after = VTAILQ_NEXT(after, src_list);
	if (!tokens_match_after(after, look_ahead_src))
		return (0);

	return (matched);
}
