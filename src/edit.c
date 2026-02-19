#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vcc_compile.h"
#include "libvcc.h"
#include "buf.h"
#include "pattern.h"
#include "format.h"
#include "edit.h"

int source_has_tokens(struct source *src) {
	struct token *t;

	t = VTAILQ_FIRST(&src->src_tokens);
	return (t != NULL && t->tok != EOI);
}

void add_boundary_tokens(struct source *src) {
	struct token *t, *soi;
	static const char soi_text[] = "SOI";
	static const char eoi_text[] = "EOI";

	/* Find EOI token and set its text */
	VTAILQ_FOREACH(t, &src->src_tokens, src_list) {
		if (t->tok == EOI) {
			t->b = eoi_text;
			t->e = eoi_text + 3;
			break;
		}
	}

	/* Allocate and prepend SOI token */
	soi = calloc(1, sizeof(*soi));
	soi->tok = SOI;
	soi->b = soi_text;
	soi->e = soi_text + 3;
	VTAILQ_INSERT_HEAD(&src->src_tokens, soi, src_list);
}

int check_unknown_gaps(struct source *src) {
	struct token *t;
	const char *last_end, *p, *start, *gap_end;

	last_end = src->b;
	VTAILQ_FOREACH(t, &src->src_tokens, src_list) {
		if (t->tok == SOI)
			continue;
		gap_end = (t->tok == EOI) ? src->e : t->b;
		p = last_end;
		while (p < gap_end) {
			while (p < gap_end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
				p++;
			if (p >= gap_end)
				break;
			start = p;
			if (*p == '/' && p + 1 < gap_end && p[1] == '*') {
				/* C-style comment: skip to closing */
				p += 2;
				while (p + 1 < gap_end && !(*p == '*' && p[1] == '/'))
					p++;
				if (p + 1 < gap_end)
					p += 2;
			}
			else {
				while (p < gap_end && *p != '\n')
					p++;
				if (*start != '#' && *start != '$' &&
				    !(*start == '/' && start + 1 < gap_end && start[1] == '/')) {
					fprintf(stderr, "syntax error: unparseable content: %.*s\n", (int)(p - start), start);
					return (-1);
				}
			}
		}
		if (t->tok == EOI)
			break;
		last_end = t->e;
	}
	return (0);
}

int text_needs_raw(const char *text) {
	int in_str;

	in_str = 0;
	for (; *text; text++) {
		if (*text == '"')
			in_str = !in_str;
		if (!in_str && (*text == '$' || *text == '#'))
			return (1);
	}
	return (0);
}

void add_comment_tokens(struct source *src) {
	struct token *t, *prev, *ct;
	const char *gap_start, *gap_end, *p, *start;

	prev = NULL;
	VTAILQ_FOREACH(t, &src->src_tokens, src_list) {
		if (t->tok == SOI) {
			prev = t;
			continue;
		}

		if (prev != NULL && prev->tok == SOI)
			gap_start = src->b;
		else if (prev != NULL)
			gap_start = prev->e;
		else
			gap_start = src->b;

		gap_end = (t->tok == EOI) ? src->e : t->b;
		p = gap_start;
		while (p < gap_end) {
			while (p < gap_end &&
			    (*p == ' ' || *p == '\t' ||
			    *p == '\n' || *p == '\r'))
				p++;
			if (p >= gap_end)
				break;
			start = p;
			if (*p == '/' && p + 1 < gap_end && p[1] == '*') {
				p += 2;
				while (p + 1 < gap_end &&
				    !(*p == '*' && p[1] == '/'))
					p++;
				if (p + 1 < gap_end)
					p += 2;
			}
			else if (*p == '#' ||
			    (*p == '/' && p + 1 < gap_end &&
			    p[1] == '/')) {
				while (p < gap_end && *p != '\n')
					p++;
			}
			else {
				while (p < gap_end && *p != '\n')
					p++;
				continue;
			}
			ct = calloc(1, sizeof(*ct));
			ct->tok = COMMENT;
			ct->b = start;
			ct->e = p;
			ct->src = src;
			VTAILQ_INSERT_BEFORE(t, ct, src_list);
		}

		prev = t;
		if (t->tok == EOI)
			break;
	}
}

void make_comment_source(struct source *src) {
	struct token *ct, *eoi;
	const char *p, *start, *end;

	p = src->b;
	while (p < src->e &&
	    (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
		p++;
	if (p >= src->e)
		return;

	start = p;
	if (*p == '/' && p + 1 < src->e && p[1] == '*') {
		p += 2;
		while (p + 1 < src->e && !(*p == '*' && p[1] == '/'))
			p++;
		if (p + 1 < src->e)
			p += 2;
	}
	else if (*p == '#' ||
	    (*p == '/' && p + 1 < src->e && p[1] == '/')) {
		while (p < src->e && *p != '\n')
			p++;
	}
	else {
		return;
	}
	end = p;

	ct = calloc(1, sizeof(*ct));
	ct->tok = COMMENT;
	ct->b = start;
	ct->e = end;
	ct->src = src;

	eoi = VTAILQ_FIRST(&src->src_tokens);
	while (eoi != NULL && eoi->tok != EOI)
		eoi = VTAILQ_NEXT(eoi, src_list);
	if (eoi != NULL)
		VTAILQ_INSERT_BEFORE(eoi, ct, src_list);
}

void lex_pattern(struct vcc *vcc, const char *text, struct source **dst, char **preprocessed) {
	char *pp;

	if (text == NULL)
		return;
	pp = preprocess_wildcards(text);
	if (pp == NULL)
		return;
	*preprocessed = pp;
	*dst = vcc_new_source(pp, "pattern", "pattern");
	vcc_Lexer(vcc, *dst);
}

static void buf_emit_replacement(struct buf *out, const struct replace_opts *rep, struct capture *caps, int ncaps) {
	struct token *t, *skip;
	int idx;
	char sbuf[4096];

	VTAILQ_FOREACH(t, &rep->to_src->src_tokens, src_list) {
		if (t->tok == EOI)
			break;
		/* Bare **N: three tokens *, *, digit */
		if (match_bare_capture(t, &idx, &skip)) {
			if (idx <= ncaps && caps[idx-1].start != NULL) {
				if (out->len > 0)
					buf_appendc(out, ' ');
				buf_append(out, caps[idx-1].start, caps[idx-1].end - caps[idx-1].start);
			}
			t = skip;
			continue;
		}
		/* CSTR with **N inside */
		if (has_capture_ref(t)) {
			substitute_captures(t->b, (size_t)(t->e - t->b), caps, ncaps, sbuf, sizeof(sbuf));
			if (out->len > 0)
				buf_appendc(out, ' ');
			buf_appends(out, sbuf);
		}
		else {
			if (out->len > 0)
				buf_appendc(out, ' ');
			buf_append(out, t->b, (size_t)(t->e - t->b));
		}
	}
}

void cmd_tokens(struct source *src, int processed) {
	struct token *t;
	const char *name, *last_end;

	printf("%-12s %s\n", "TYPE", "VALUE");
	printf("%-12s %s\n", "----", "-----");
	last_end = src->b;
	VTAILQ_FOREACH(t, &src->src_tokens, src_list) {
		if (t->tok == SOI) {
			if (processed)
				printf("%-12s %.*s\n", "SOI", PF(t));
			continue;
		}
		if (processed && last_end != NULL)
			emit_gap(last_end, t->tok == EOI ? src->e : t->b);
		if (t->tok == EOI) {
			if (processed)
				printf("%-12s %.*s\n", "EOI", PF(t));
			break;
		}
		if (t->tok < 256)
			name = vcl_tnames[t->tok];
		else
			name = NULL;
		if (name != NULL)
			printf("%-12s %.*s\n", name, PF(t));
		else
			printf("?%-11u %.*s\n", t->tok, PF(t));
		last_end = t->e;
	}
}

char *emit_transform_replace(struct source *src, const struct replace_opts *rep) {
	struct buf out;
	struct token *t, *prev;
	int rep_count;
	struct token *from_pat[128];
	struct capture caps[MAX_CAPTURES];
	int from_npat, matched, ncaps, i;

	buf_init(&out);
	prev = NULL;
	rep_count = 0;
	from_npat = 0;
	if (rep->from_src != NULL)
		from_npat = build_pattern(rep->from_src, from_pat);

	for (t = VTAILQ_FIRST(&src->src_tokens); t != NULL; ) {
		if (t->tok == EOI)
			break;
		if (t->tok == SOI) {
			prev = t;
			t = VTAILQ_NEXT(t, src_list);
			continue;
		}

		if (from_npat > 0 && (rep->match.limit == 0 || rep_count < rep->match.offset + rep->match.limit)) {
			matched = try_pattern_match(t, prev, from_pat, from_npat,
			    rep->match.look_behind_src, rep->match.look_ahead_src, caps, &ncaps);
			if (matched > 0) {
				rep_count++;
				if (rep_count <= rep->match.offset) {
					/* Within offset -- emit originals */
					for (i = 0; i < matched; i++) {
						if (out.len > 0)
							buf_appendc(&out, ' ');
						buf_append(&out, t->b, (size_t)(t->e - t->b));
						prev = t;
						t = VTAILQ_NEXT(t, src_list);
					}
					continue;
				}
				for (i = 0; i < matched; i++) {
					prev = t;
					t = VTAILQ_NEXT(t, src_list);
				}
				buf_emit_replacement(&out, rep, caps, ncaps);
				continue;
			}
		}

		if (out.len > 0)
			buf_appendc(&out, ' ');
		buf_append(&out, t->b, (size_t)(t->e - t->b));
		prev = t;
		t = VTAILQ_NEXT(t, src_list);
	}

	buf_appendc(&out, '\0');
	return (out.data);
}

void emit_formatted(struct source *src, const struct insert_opts *ins, const struct replace_opts *rep) {
	struct token *t, *prev;
	struct fmt_state st;
	int before_ok, after_ok;
	int ins_count, rep_count;
	struct token *from_pat[128];
	struct capture caps[MAX_CAPTURES];
	int from_npat, matched, ncaps, i;
	const char *last_end;

	memset(&st, 0, sizeof(st));
	st.first = 1;
	prev = NULL;
	ins_count = 0;
	rep_count = 0;
	from_npat = 0;
	last_end = src->b;
	if (rep != NULL && rep->from_src != NULL)
		from_npat = build_pattern(rep->from_src, from_pat);

	for (t = VTAILQ_FIRST(&src->src_tokens); t != NULL; ) {
		if (t->tok == EOI)
			break;
		if (t->tok == SOI) {
			prev = t;
			t = VTAILQ_NEXT(t, src_list);
			continue;
		}

		/* Emit comments from the gap before this token */
		if (last_end != NULL && t->b > last_end)
			fmt_emit_gap_comments(&st, last_end, t->b);

		/* Insert: inject formatted tokens at match point */
		if (ins != NULL && ins->src != NULL &&
		    (ins->match.look_behind_src != NULL || ins->match.look_ahead_src != NULL) &&
		    (ins->match.limit == 0 || ins_count < ins->match.offset + ins->match.limit)) {
			before_ok = tokens_match_before(prev, ins->match.look_behind_src);
			after_ok = tokens_match_after(t, ins->match.look_ahead_src);
			if (before_ok && after_ok) {
				ins_count++;
				if (ins_count > ins->match.offset)
					fmt_emit_source(&st, ins->src);
			}
		}

		/* Replace: match from pattern and emit to pattern */
		if (rep != NULL && from_npat > 0 && (rep->match.limit == 0 || rep_count < rep->match.offset + rep->match.limit)) {
			matched = try_pattern_match(t, prev, from_pat, from_npat,
			    rep->match.look_behind_src, rep->match.look_ahead_src, caps, &ncaps);
			if (matched > 0) {
				rep_count++;
				if (rep_count <= rep->match.offset) {
					/* Within offset -- emit originals */
					for (i = 0; i < matched; i++) {
						fmt_emit(&st, t, NULL);
						prev = t;
						last_end = t->e;
						t = VTAILQ_NEXT(t, src_list);
					}
					continue;
				}
				for (i = 0; i < matched; i++) {
					prev = t;
					t = VTAILQ_NEXT(t, src_list);
				}
				if (!rep->to_raw && rep->to_src != NULL && source_has_tokens(rep->to_src)) {
					fmt_emit_source_caps(&st, rep->to_src, caps, ncaps);
				}
				else {
					char rbuf[4096];
					substitute_captures(
						rep->to_text,
						strlen(rep->to_text),
						caps,
						ncaps,
						rbuf,
						sizeof(rbuf)
					);
					fmt_emit_raw(&st, rbuf);
				}
				/* Preserve line-break from last consumed source token */
				if (prev->tok == ';' || prev->tok == '{' || prev->tok == '}') {
					st.need_newline = 1;
					if (st.indent == 0)
						st.need_blank = 1;
				}
				last_end = prev->e;
				continue;
			}
		}

		fmt_emit(&st, t, NULL);
		prev = t;
		last_end = t->e;
		t = VTAILQ_NEXT(t, src_list);
	}

	/* Insert with no constraints -- append to end */
	if (ins != NULL && ins->src != NULL &&
		ins->match.look_behind_src == NULL && ins->match.look_ahead_src == NULL)
		fmt_emit_source(&st, ins->src);

	printf("\n");
}

void cmd_extract(struct source *src, const struct extract_opts *ext) {
	struct token *t, *prev, *end;
	struct token *from_pat[128];
	struct capture caps[MAX_CAPTURES];
	int from_npat, matched, ncaps, i, count;
	const char *p, *q;
	char rbuf[4096];

	from_npat = 0;
	if (ext->from_src != NULL)
		from_npat = build_pattern(ext->from_src, from_pat);
	if (from_npat == 0)
		return;

	prev = NULL;
	count = 0;
	for (t = VTAILQ_FIRST(&src->src_tokens); t != NULL; ) {
		if (t->tok == EOI)
			break;
		if (t->tok == SOI) {
			prev = t;
			t = VTAILQ_NEXT(t, src_list);
			continue;
		}

		if (ext->match.limit > 0 && count >= ext->match.offset + ext->match.limit)
			break;

		matched = try_pattern_match(t, prev, from_pat, from_npat,
		    ext->match.look_behind_src, ext->match.look_ahead_src,
		    caps, &ncaps);
		if (matched > 0) {
			count++;
			if (count <= ext->match.offset) {
				for (i = 0; i < matched; i++) {
					prev = t;
					t = VTAILQ_NEXT(t, src_list);
				}
				continue;
			}
			if (ext->to_text != NULL) {
				/* 2-arg mode: fixup gap captures, then substitute */
				fixup_gap_captures(
					t,
					from_pat,
					from_npat,
					caps,
					ncaps
				);
				substitute_captures(
					ext->to_text,
					strlen(ext->to_text),
					caps,
					ncaps,
					rbuf,
					sizeof(rbuf)
				);
				p = rbuf;
				q = p + strlen(p);
			}
			else {
				/* 1-arg mode: print raw matched text */
				end = t;
				for (i = 1; i < matched; i++)
					end = VTAILQ_NEXT(end, src_list);
				p = t->b;
				q = end->e;
			}
			/* Strip leading/trailing newlines (always) */
			while (p < q && *p == '\n')
				p++;
			while (q > p && q[-1] == '\n')
				q--;
			if (ext->strip_ws) {
				/* Dedent: find minimum leading whitespace */
				const char *lp;
				int min_indent, indent;
				min_indent = -1;
				lp = p;
				while (lp < q) {
					indent = 0;
					while (lp + indent < q &&
					    (lp[indent] == ' ' || lp[indent] == '\t'))
						indent++;
					if (lp + indent < q && lp[indent] != '\n') {
						if (min_indent < 0 || indent < min_indent)
							min_indent = indent;
					}
					while (lp < q && *lp != '\n')
						lp++;
					if (lp < q)
						lp++;
				}
				if (min_indent < 0)
					min_indent = 0;
				/* Print each line with common indent removed */
				lp = p;
				while (lp < q) {
					const char *le = lp;
					while (le < q && *le != '\n')
						le++;
					indent = 0;
					while (indent < min_indent &&
					    lp + indent < le &&
					    (lp[indent] == ' ' || lp[indent] == '\t'))
						indent++;
					if (lp != p)
						putchar('\n');
					printf("%.*s", (int)(le - lp - indent), lp + indent);
					lp = le;
					if (lp < q)
						lp++;
				}
				putchar('\n');
			}
			else {
				printf("%.*s\n", (int)(q - p), p);
			}
			for (i = 0; i < matched; i++) {
				prev = t;
				t = VTAILQ_NEXT(t, src_list);
			}
			continue;
		}

		prev = t;
		t = VTAILQ_NEXT(t, src_list);
	}
}
