#!/bin/bash
set -euo pipefail

BINARY="${BINARY:-./dist/vinyl-edit}"
TEST_DIR="test"
pass=0
fail=0
errors=""

if [ ! -x "$BINARY" ]; then
	echo "ERROR: $BINARY not found, run 'make build' first"
	exit 1
fi

echo ""
echo " STATUS  TEST-NAME"

for test_file in "$TEST_DIR"/*; do
	# only run .success and .fail files
	expect_rc=0
	case "$test_file" in
		*.success) expect_rc=0 ;;
		*.fail) expect_rc=1 ;;
		*) continue ;;
	esac
	[ -f "$test_file" ] || continue

	name=$(basename "$test_file")

	# Parse three sections separated by === lines:
	#   === \n command \n === \n input \n === \n expected
	cmd=""
	input=""
	expected=""
	section=0

	while IFS= read -r line || [ -n "$line" ]; do
		if [ "$line" = "===" ]; then
			section=$((section + 1))
			continue
		fi
		case $section in
		1) cmd="${cmd:+$cmd }${line}" ;;
		2) input="${input}${line}
" ;;
		3) expected="${expected}${line}
" ;;
		esac
	done < "$test_file"

	# Write input to temp file
	tmp=$(mktemp)
	printf '%s' "$input" > "$tmp"

	# Check for pipe: prefix — pipe stdin instead of file arg
	use_pipe=0
	run_cmd="$cmd"
	if [ "${cmd#pipe:}" != "$cmd" ]; then
		use_pipe=1
		run_cmd="${cmd#pipe:}"
	fi

	# Split into command word and remaining args
	cmd_word="${run_cmd%% *}"
	if [ "$cmd_word" = "$run_cmd" ]; then
		cmd_rest=""
	else
		cmd_rest="${run_cmd#* }"
	fi

	# Run command: <command> <file|-> [args...]
	actual_rc=0
	if [ "$use_pipe" -eq 1 ]; then
		actual=$(eval "\"$BINARY\" $cmd_word - $cmd_rest" < "$tmp" 2>&1) || actual_rc=$?
	else
		actual=$(eval "\"$BINARY\" $cmd_word \"$tmp\" $cmd_rest" 2>&1) || actual_rc=$?
	fi

	# Normalize temp file paths in dry-run output
	case "$cmd" in
		*--dry-run*) actual=$(printf '%s' "$actual" | sed "s|$tmp|FILE|g") ;;
	esac

	rm -f "$tmp"

	output_match=0
	if [ "$actual" = "$(printf '%s' "$expected")" ]; then
		output_match=1
	fi

	if [ "$actual_rc" -eq "$expect_rc" ] && [ "$output_match" -eq 1 ]; then
		printf " passed  %s\n" "$name"
		pass=$((pass + 1))
	else
		printf " failed  %s\n" "$name"
		fail=$((fail + 1))
		errors="${errors}\n*** rc=$actual_rc $name ***\n\n"
		errors="${errors}$(diff -u <(printf '%s' "$expected") <(printf '%s' "$actual") || true)\n"
	fi
done

echo ""
echo " $pass passed, $fail failed"

if [ -n "$errors" ]; then
	printf "%b\n" "$errors"
	exit 1
else
	echo ""
	exit 0
fi
