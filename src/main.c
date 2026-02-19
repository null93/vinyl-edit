#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "vcc_compile.h"
#include "libvcc.h"
#include "edit.h"

static int parse_common_flag(
    int argc, char **argv, int *i, struct match_constraint *mc) {
	if (strcmp(argv[*i], "--look-behind") == 0) {
		if (*i + 1 >= argc) {
			fprintf(stderr, "--look-behind requires a value\n");
			return (-1);
		}
		mc->look_behind = argv[++(*i)];
		return (1);
	}
	else if (strcmp(argv[*i], "--look-ahead") == 0) {
		if (*i + 1 >= argc) {
			fprintf(stderr, "--look-ahead requires a value\n");
			return (-1);
		}
		mc->look_ahead = argv[++(*i)];
		return (1);
	}
	else if (strcmp(argv[*i], "--limit") == 0) {
		if (*i + 1 >= argc) {
			fprintf(stderr, "--limit requires a value\n");
			return (-1);
		}
		mc->limit = atoi(argv[++(*i)]);
		return (1);
	}
	else if (strcmp(argv[*i], "--offset") == 0) {
		if (*i + 1 >= argc) {
			fprintf(stderr, "--offset requires a value\n");
			return (-1);
		}
		mc->offset = atoi(argv[++(*i)]);
		return (1);
	}
	return (0);
}

static int parse_insert_opts(int argc, char **argv, struct insert_opts *opts) {
	int r;

	memset(opts, 0, sizeof(*opts));

	for (int i = 0; i < argc; i++) {
		r = parse_common_flag(argc, argv, &i, &opts->match);
		if (r < 0)
			return (-1);
		if (r > 0)
			continue;
		if (argv[i][0] == '-' && argv[i][1] == '-') {
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
			return (-1);
		}
		if (opts->text == NULL) {
			opts->text = argv[i];
		}
		else {
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
			return (-1);
		}
	}

	if (opts->text == NULL) {
		fprintf(stderr, "insert requires text to insert\n");
		return (-1);
	}
	if (opts->match.offset > 0 && opts->match.limit == 0) {
		fprintf(stderr, "--offset requires --limit\n");
		return (-1);
	}
	return (0);
}

static int parse_replace_opts(int argc, char **argv, struct replace_opts *opts) {
	int r;

	memset(opts, 0, sizeof(*opts));

	for (int i = 0; i < argc; i++) {
		r = parse_common_flag(argc, argv, &i, &opts->match);
		if (r < 0)
			return (-1);
		if (r > 0)
			continue;
		if (argv[i][0] == '-' && argv[i][1] == '-') {
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
			return (-1);
		}
		if (opts->from_value == NULL) {
			opts->from_value = argv[i];
		}
		else if (opts->to_text == NULL) {
			opts->to_text = argv[i];
		}
		else {
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
			return (-1);
		}
	}

	if (opts->from_value == NULL || opts->to_text == NULL) {
		fprintf(stderr, "replace requires <from> and <to> values\n");
		return (-1);
	}
	if (opts->match.offset > 0 && opts->match.limit == 0) {
		fprintf(stderr, "--offset requires --limit\n");
		return (-1);
	}
	return (0);
}

static int parse_extract_opts(int argc, char **argv, struct extract_opts *opts) {
	int r;

	memset(opts, 0, sizeof(*opts));

	for (int i = 0; i < argc; i++) {
		r = parse_common_flag(argc, argv, &i, &opts->match);
		if (r < 0)
			return (-1);
		if (r > 0)
			continue;
		if (strcmp(argv[i], "--strip-whitespace") == 0) {
			opts->strip_ws = 1;
			continue;
		}
		if (argv[i][0] == '-' && argv[i][1] == '-') {
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
			return (-1);
		}
		if (opts->from_value == NULL) {
			opts->from_value = argv[i];
		}
		else if (opts->to_text == NULL) {
			opts->to_text = argv[i];
		}
		else {
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
			return (-1);
		}
	}

	if (opts->from_value == NULL) {
		fprintf(stderr, "extract requires a pattern\n");
		return (-1);
	}
	if (opts->match.offset > 0 && opts->match.limit == 0) {
		fprintf(stderr, "--offset requires --limit\n");
		return (-1);
	}
	return (0);
}

static int is_command(const char *arg) {
	return (strcmp(arg, "format") == 0 ||
		strcmp(arg, "tokens") == 0 ||
		strcmp(arg, "insert") == 0 ||
		strcmp(arg, "replace") == 0 ||
		strcmp(arg, "extract") == 0);
}

static char *read_stdin(long *out_len) {
	char *buf;
	size_t cap, len, n;

	cap = 4096;
	len = 0;
	buf = malloc(cap);
	if (buf == NULL)
		return (NULL);
	while ((n = fread(buf + len, 1, cap - len, stdin)) > 0) {
		len += n;
		if (len == cap) {
			cap *= 2;
			buf = realloc(buf, cap);
			if (buf == NULL)
				return (NULL);
		}
	}
	buf[len] = '\0';
	*out_len = (long)len;
	return (buf);
}

static void usage(const char *progname) {
	fprintf(stderr,
		"Usage: %s <command> <file> [flags] [args]\n"
		"\n"
		"Global Flags:\n"
		"  --dry-run                    Show a unified diff instead of writing output\n"
		"  --no-color                   Disable colored diff output\n"
		"\n"
		"Commands:\n"
		"  format  <file> [flags]                        Pretty-print VCL source\n"
		"  tokens  <file> [flags]                        Print the token stream\n"
		"  insert  <file> <text> [flags]                 Insert text at a matched position\n"
		"  replace <file> <from> <to> [flags]            Replace matched tokens\n"
		"  extract <file> <pattern> [template] [flags]   Extract matching regions\n"
		"\n"
		"Tokens Flags:\n"
		"  --processed                  Include SOI/EOI markers and inter-token gaps\n"
		"\n"
		"Insert Flags:\n"
		"  --look-behind <pattern>      Require these tokens before the insertion point\n"
		"  --look-ahead  <pattern>      Require these tokens after the insertion point\n"
		"  --limit <n>                  Max insertions (default: unlimited)\n"
		"  --offset <n>                 Skip first n matches (requires --limit)\n"
		"\n"
		"Replace Flags:\n"
		"  --look-behind <pattern>      Require these tokens before the match\n"
		"  --look-ahead  <pattern>      Require these tokens after the match\n"
		"  --limit <n>                  Max replacements (default: unlimited)\n"
		"  --offset <n>                 Skip first n matches (requires --limit)\n"
		"\n"
		"Extract Flags:\n"
		"  --look-behind <pattern>      Require these tokens before the match\n"
		"  --look-ahead  <pattern>      Require these tokens after the match\n"
		"  --limit <n>                  Max extractions (default: unlimited)\n"
		"  --offset <n>                 Skip first n matches (requires --limit)\n"
		"  --strip-whitespace           Dedent and trim extracted output\n"
		"\n"
		"Wildcards:\n"
		"  **                           Match any single token\n"
		"  ***                          Match zero or more tokens (non-greedy)\n"
		"  **1..**9                     Back-reference a captured wildcard\n"
		"\n"
		"Boundary tokens:\n"
		"  SOI                          Start of input\n"
		"  EOI                          End of input\n"
		"\n"
		"Examples:\n"
		"  # Pretty-print a VCL file\n"
		"  %s format default.vcl\n"
		"\n"
		"  # Format from stdin\n"
		"  cat default.vcl | %s format -\n"
		"\n"
		"  # Show the processed token stream with SOI/EOI and gap content\n"
		"  %s tokens default.vcl --processed\n"
		"\n"
		"  # Insert an import statement after the vcl declaration\n"
		"  %s insert default.vcl 'import std;' --look-behind 'SOI vcl **;'\n"
		"\n"
		"  # Replace all backend hosts with a new address\n"
		"  %s replace default.vcl '.host = **' '.host = \"10.10.10.10\"'\n"
		"\n"
		"  # Preview replacing the first timeout with a diff\n"
		"  %s replace default.vcl '.timeout = **' '.timeout = 5s' --limit 1 --dry-run\n"
		"\n"
		"  # Append an entry to every ACL\n"
		"  %s replace default.vcl 'acl ** {***}' 'acl **1 {**2 \"10.0.0.0\"/8;}'\n"
		"\n"
		"  # Remove all .probe blocks from backend definitions\n"
		"  %s replace default.vcl '.probe = {***}' ''\n"
		"\n"
		"  # Extract ACL entries with whitespace stripped\n"
		"  %s extract default.vcl 'acl ** {***}' '**2' --strip-whitespace\n"
		"\n"
		"  # Extract all subroutine definitions\n"
		"  %s extract default.vcl 'sub ** {***}'\n"
		"\n"
		"  # Extract the second backend block, skipping the first\n"
		"  %s extract default.vcl 'backend ** {***}' --limit 1 --offset 1\n",
		progname, progname, progname, progname, progname, progname,
		progname, progname, progname, progname, progname, progname
	);
}

struct dry_run_state {
	char orig_tmp[64];
	char out_tmp[64];
	int saved_stdout;
};

static int setup_dry_run(struct dry_run_state *dr, const char *buf, long len) {
	int orig_fd, out_fd;

	strlcpy(dr->orig_tmp, "/tmp/vinyl-edit-orig.XXXXXX", sizeof(dr->orig_tmp));
	strlcpy(dr->out_tmp, "/tmp/vinyl-edit-out.XXXXXX", sizeof(dr->out_tmp));

	orig_fd = mkstemp(dr->orig_tmp);
	if (orig_fd < 0) {
		perror("mkstemp");
		return (-1);
	}
	write(orig_fd, buf, len);
	close(orig_fd);

	out_fd = mkstemp(dr->out_tmp);
	if (out_fd < 0) {
		perror("mkstemp");
		unlink(dr->orig_tmp);
		return (-1);
	}

	fflush(stdout);
	dr->saved_stdout = dup(STDOUT_FILENO);
	dup2(out_fd, STDOUT_FILENO);
	close(out_fd);
	return (0);
}

static int finish_dry_run(struct dry_run_state *dr, const char *input_name, int no_color) {
	int diff_ret, diff_status;
	char diff_cmd[1024];

	fflush(stdout);
	dup2(dr->saved_stdout, STDOUT_FILENO);
	close(dr->saved_stdout);

	snprintf(
		diff_cmd,
		sizeof(diff_cmd),
		"diff -u %s--label \"a/%s\" --label \"b/%s\" \"%s\" \"%s\"",
		no_color ? "" : "--color ",
		input_name,
		input_name,
		dr->orig_tmp,
		dr->out_tmp
	);
	diff_ret = system(diff_cmd);
	unlink(dr->orig_tmp);
	unlink(dr->out_tmp);

	if (WIFEXITED(diff_ret))
		diff_status = WEXITSTATUS(diff_ret);
	else
		diff_status = 2;
	return (diff_status >= 2 ? 1 : 0);
}

static int cmd_insert(struct vcc *vcc, struct source *src, int argc, char **argv) {
	struct insert_opts iopts;
	struct source *ins_src;
	struct vcc *pat_vcc;
	char *mb_pp = NULL, *ma_pp = NULL;

	(void)vcc;
	if (parse_insert_opts(argc, argv, &iopts) != 0)
		return (-1);
	pat_vcc = VCC_New();
	ins_src = vcc_new_source(iopts.text, "insert", "insert");
	vcc_Lexer(pat_vcc, ins_src);
	iopts.src = ins_src;
	lex_pattern(pat_vcc, iopts.match.look_behind, &iopts.match.look_behind_src, &mb_pp);
	lex_pattern(pat_vcc, iopts.match.look_ahead, &iopts.match.look_ahead_src, &ma_pp);
	emit_formatted(src, &iopts, NULL);
	free(mb_pp);
	free(ma_pp);
	return (0);
}

static int cmd_replace(struct vcc *vcc, struct source *src, int argc, char **argv) {
	struct replace_opts ropts;
	struct vcc *pat_vcc;
	char *from_pp = NULL, *to_pp = NULL;
	char *mb_pp = NULL, *ma_pp = NULL;

	(void)vcc;
	if (parse_replace_opts(argc, argv, &ropts) != 0)
		return (-1);
	pat_vcc = VCC_New();
	lex_pattern(pat_vcc, ropts.match.look_behind, &ropts.match.look_behind_src, &mb_pp);
	lex_pattern(pat_vcc, ropts.match.look_ahead, &ropts.match.look_ahead_src, &ma_pp);
	lex_pattern(pat_vcc, ropts.from_value, &ropts.from_src, &from_pp);
	if (ropts.to_text != NULL && text_needs_raw(ropts.to_text)) {
		ropts.to_raw = 1;
	}
	else {
		lex_pattern(pat_vcc, ropts.to_text, &ropts.to_src, &to_pp);
	}
	if (ropts.to_raw) {
		emit_formatted(src, NULL, &ropts);
	}
	else {
		char *raw;
		struct source *raw_src;
		raw = emit_transform_replace(src, &ropts);
		raw_src = vcc_new_source(raw, "transformed", "transformed");
		vcc_Lexer(pat_vcc, raw_src);
		emit_formatted(raw_src, NULL, NULL);
		free(raw);
	}
	free(from_pp);
	free(to_pp);
	free(mb_pp);
	free(ma_pp);
	return (0);
}

static int cmd_extract_main(struct vcc *vcc, struct source *src, int argc, char **argv) {
	struct extract_opts eopts;
	struct vcc *pat_vcc;
	char *from_pp = NULL, *to_pp = NULL;
	char *mb_pp = NULL, *ma_pp = NULL;

	(void)vcc;
	if (parse_extract_opts(argc, argv, &eopts) != 0)
		return (-1);
	pat_vcc = VCC_New();
	lex_pattern(pat_vcc, eopts.match.look_behind, &eopts.match.look_behind_src, &mb_pp);
	lex_pattern(pat_vcc, eopts.match.look_ahead, &eopts.match.look_ahead_src, &ma_pp);
	lex_pattern(pat_vcc, eopts.from_value, &eopts.from_src, &from_pp);
	if (eopts.from_src != NULL && !source_has_tokens(eopts.from_src))
		make_comment_source(eopts.from_src);
	if (eopts.to_text != NULL) {
		if (text_needs_raw(eopts.to_text)) {
			eopts.to_raw = 1;
		}
		else {
			lex_pattern(pat_vcc, eopts.to_text, &eopts.to_src, &to_pp);
		}
	}
	add_comment_tokens(src);
	cmd_extract(src, &eopts);
	free(from_pp);
	free(to_pp);
	free(mb_pp);
	free(ma_pp);
	return (0);
}

int main(int argc, char *argv[]) {
	struct vcc *vcc;
	struct source *src;
	FILE *f;
	long len;
	char *buf;
	const char *cmd;
	const char *input_name;
	int dry_run, no_color, use_stdin;
	int opt_argc, i, j;
	char **opt_argv;

	if (argc < 3) {
		usage(argv[0]);
		return (1);
	}

	/* Phase 1: command and file path */
	cmd = argv[1];
	if (!is_command(cmd)) {
		fprintf(stderr, "Unknown command: %s\n", cmd);
		usage(argv[0]);
		return (1);
	}

	if (strcmp(argv[2], "-") == 0) {
		use_stdin = 1;
		input_name = "stdin";
	}
	else {
		use_stdin = 0;
		input_name = argv[2];
	}

	/* Phase 2: strip global flags from remaining args */
	opt_argv = argv + 3;
	opt_argc = argc - 3;
	dry_run = 0;
	no_color = 0;
	j = 0;
	for (i = 0; i < opt_argc; i++) {
		if (strcmp(opt_argv[i], "--dry-run") == 0) {
			dry_run = 1;
		}
		else if (strcmp(opt_argv[i], "--no-color") == 0) {
			no_color = 1;
		}
		else {
			opt_argv[j++] = opt_argv[i];
		}
	}
	opt_argc = j;

	/* Phase 3: read input */
	if (use_stdin) {
		buf = read_stdin(&len);
		if (buf == NULL) {
			perror("read_stdin");
			return (1);
		}
	}
	else {
		f = fopen(argv[2], "r");
		if (f == NULL) {
			perror(argv[2]);
			return (1);
		}
		fseek(f, 0, SEEK_END);
		len = ftell(f);
		fseek(f, 0, SEEK_SET);
		buf = malloc(len + 1);
		if (buf == NULL) {
			perror("malloc");
			fclose(f);
			return (1);
		}
		if (fread(buf, 1, len, f) != (size_t)len) {
			perror("fread");
			free(buf);
			fclose(f);
			return (1);
		}
		buf[len] = '\0';
		fclose(f);
	}

	vcc = VCC_New();
	src = vcc_new_source(buf, "file", input_name);
	vcc_Lexer(vcc, src);
	add_boundary_tokens(src);

	/* Check for unparseable content (skip for tokens -- it's diagnostic) */
	if (strcmp(cmd, "tokens") != 0 && check_unknown_gaps(src) != 0) {
		free(buf);
		return (1);
	}

	/* Phase 4: --dry-run capture setup */
	struct dry_run_state dr;

	if (dry_run) {
		if (setup_dry_run(&dr, buf, len) != 0) {
			free(buf);
			return (1);
		}
	}

	/* Command dispatch */
	if (strcmp(cmd, "format") == 0) {
		if (opt_argc > 0) {
			fprintf(stderr, "Unknown option: %s\n", opt_argv[0]);
			free(buf);
			return (1);
		}
		emit_formatted(src, NULL, NULL);
	}
	else if (strcmp(cmd, "tokens") == 0) {
		int processed = 0;
		for (i = 0; i < opt_argc; i++) {
			if (strcmp(opt_argv[i], "--processed") == 0)
				processed = 1;
			else {
				fprintf(stderr, "Unknown option: %s\n", opt_argv[i]);
				free(buf);
				return (1);
			}
		}
		cmd_tokens(src, processed);
	}
	else if (strcmp(cmd, "insert") == 0) {
		if (cmd_insert(vcc, src, opt_argc, opt_argv) != 0) {
			free(buf);
			return (1);
		}
	}
	else if (strcmp(cmd, "replace") == 0) {
		if (cmd_replace(vcc, src, opt_argc, opt_argv) != 0) {
			free(buf);
			return (1);
		}
	}
	else if (strcmp(cmd, "extract") == 0) {
		if (cmd_extract_main(vcc, src, opt_argc, opt_argv) != 0) {
			free(buf);
			return (1);
		}
	}
	else {
		fprintf(stderr, "Unknown command: %s\n", cmd);
		usage(argv[0]);
		free(buf);
		return (1);
	}

	/* Phase 5: --dry-run diff */
	if (dry_run) {
		free(buf);
		return (finish_dry_run(&dr, input_name, no_color));
	}

	free(buf);
	return (0);
}
