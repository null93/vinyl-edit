<div align="center">

# vinyl-edit

**A CLI tool for editing [Vinyl VCL](https://vinyl-cache.org/docs/trunk/reference/vcl.html) files.**


[![Vibe Coded](https://img.shields.io/badge/CODED_WITH-%E2%9C%A8_VIBES_%E2%9C%A8-ff69b4?style=for-the-badge)](#)
[![Claude](https://img.shields.io/badge/Claude-D97757?style=for-the-badge&logo=claude&logoColor=fff)](#)
[![C](https://img.shields.io/badge/Language-00599C?style=for-the-badge&logo=c&logoColor=white)](#)
[![macOS](https://img.shields.io/badge/macOS-000000?style=for-the-badge&logo=apple&logoColor=white)](#)
[![Linux](https://img.shields.io/badge/Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)](#)


Built for pipelines, config management, and anyone tired of fragile `sed` commands touching production VCL.

</div>

---

## Features

| Feature | Description |
| --- | --- |
| Formatting | Pretty-print VCL with consistent indentation and spacing via `format`. |
| Insertion | Inject VCL text at a structurally matched position via `insert`. |
| Find & Replace | Find and replace token patterns with wildcard and capture support via `replace`. |
| Extraction | Pattern-match against token streams and print matching regions or templated captures via `extract`. |
| Dry Run | Preview changes as a unified diff before applying with `--dry-run`. |
| Composable | Pipe commands together to chain multiple edits in one pass. |
| Token Debugging | Dump the token stream for debugging via `tokens`. |
| Native Lexing | Uses the actual Vinyl lexer (via libvcc) for structural awareness. |

## Quick Start

<details open>
<summary>Pretty-print a VCL file</summary>

```sh
vinyl-edit format default.vcl

# or pipe it and read from stdin
cat default.vcl | vinyl-edit format -
```
</details>

<details>
<summary>Replace host and port values in first backend</summary>

```sh
cat default.vcl \
  | vinyl-edit replace - '.host = **' '.host = "newhost.example.com"' --limit 1 \
  | vinyl-edit replace - '.port = **' '.port = "8080"' --limit 1
```
</details>

<details>
<summary>Append IP to all ACLs</summary>

```sh
vinyl-edit replace default.vcl 'acl ** {***}' 'acl **1 {**2 "10.0.0.1"/32;}'
```
</details>

<details>
<summary>Remove an IP from an named ACL</summary>

```sh
vinyl-edit replace default.vcl 'acl purge { *** "10.0.0.1"/32; ***}' 'acl purge {**1**2}'
```
</details>

<details>
<summary>Show all backend definitions</summary>

```sh
vinyl-edit extract default.vcl 'backend ** {***}'
```
</details>

<details>
<summary>Extract all entries from ACLs (strip leading/trailing whitespace)</summary>

```sh
vinyl-edit extract default.vcl 'acl ** {***}' '**2' --strip-whitespace
```
</details>

## Token Matching

Patterns in vinyl-edit operate on tokens, not raw text. The VCL source is first parsed into a token stream using the native Vinyl lexer, and patterns are matched against that stream. This means whitespace and line breaks don't matter.

Note that the input VCL must be syntactically valid. The lexer will throw an error if it encounters unknown tokens. However, it does not check that the VCL is semantically correct. `".host=**"` matches the same as `".host = **"` because the lexer produces the same tokens either way. Patterns can span multiple lines without any special syntax.

Literal tokens in a pattern must match exactly. For example, the pattern `sub vcl_recv {` matches those three tokens in sequence regardless of how the original source is spaced or indented.

## Wildcards

| Pattern | Matches |
| --- | --- |
| `**` | Exactly one token |
| `***` | Zero or more tokens (non-greedy) |

Use `**` when you know the structure but not the value, like `.host = **` to match any single host value. Use `***` to span unknown regions, like `acl ** {***}` to match an entire ACL body of any length.

## Anchors

Two special boundary tokens can be used to pin a pattern to the start or end of the file:

| Anchor | Position |
| --- | --- |
| `SOI` | Start of input |
| `EOI` | End of input |

For example, `SOI vcl **;` matches the VCL version declaration only if it appears at the very beginning of the file.

## Captures

In `replace` and `extract`, wildcards become numbered capture groups in the order they appear. Reference them with `**1`, `**2`, etc.

For example, given the pattern `acl ** {***}`, `**1` captures the ACL name and `**2` captures the body. A replacement of `acl **1 {**2 "10.0.0.1"/32;}` preserves the original name and body while appending a new entry.

In `extract`, captures work the same way via the template argument. Running `extract 'acl ** {***}' '**2'` prints just the body of each ACL, discarding the surrounding structure.

## Install

**macOS (Apple Silicon):**
```sh
curl -sLo /usr/local/bin/vinyl-edit https://github.com/null93/vinyl-edit/releases/download/0.0.1/vinyl-edit_0.0.0_darwin_arm64
chmod +x /usr/local/bin/vinyl-edit
```

**Linux (ARM64):**
```sh
curl -sLo /usr/local/bin/vinyl-edit https://github.com/null93/vinyl-edit/releases/download/0.0.1/vinyl-edit_0.0.0_linux_arm64
chmod +x /usr/local/bin/vinyl-edit
```

**Linux (AMD64):**
```sh
curl -sLo /usr/local/bin/vinyl-edit https://github.com/null93/vinyl-edit/releases/download/0.0.1/vinyl-edit_0.0.0_linux_amd64
chmod +x /usr/local/bin/vinyl-edit
```

## Build

In order to build the Vinyl Cache libraries, you will need to install the following dependencies:

**macOS (Homebrew):**
```sh
brew install autoconf automake docutils sphinx-doc libtool pkgconf pcre2
```

**Debian/Ubuntu:**
```sh
apt install build-essential autoconf automake libtool pkg-config libpcre2-dev
```

You can then build the vinyl-edit binary with:

```sh
make build
```

The first build will automatically compile the vendored Vinyl lexer library. The binary lands in `dist/vinyl-edit`.

## Test

Project was built with TDD, so there are a few tests located in the `test` directory.
You can run them suite with:

```sh
make test
```
