# Notes for AI agents (and humans) working on this tree

## Code style: BSD style(9) -- please, really

All C and C++ code here follows FreeBSD's
[style(9)](https://man.freebsd.org/cgi/man.cgi?query=style&sektion=9).
The plugin is C++ because libwebm's parser is C++; that is no reason to
switch to Google, LLVM or GNOME conventions.  The tests are C, in the same
style.  In particular:

* Indent with TABs (width 8); continuation lines are indented by 4 spaces.
* At most 80 columns, counting a TAB as 8.
* A function's return type is on its own line, the name starts in column 0
  and the opening brace has a line of its own.  Other braces are K&R style.
* `return (value);` -- parenthesized.
* One declaration block at the top of a function, then a blank line.
* Pointer asterisks bind to the name: `char *p`.
* A space after `if`, `for`, `while`, `switch`; none after function names.
* `/* ... */` in C, `//` is fine in C++.  No trailing-underscore or
  camelCase noise in C code; keep names consistent with their neighbours.
* No braces around single statements.

`.clang-format` approximates all of this.  Run `clang-format -i` on every file
you touch and then still read the result: it is not a substitute for knowing
the rules (it cannot place blank lines after declarations, for instance).

Match the surrounding code: comment density, naming, idiom.

## Build and test

    CXXFLAGS=-I/opt/include LDFLAGS=-L/opt/lib meson setup build
    ninja -C build
    meson test -C build

`/opt` is LOCALBASE on the author's machine; libwebm has no pkg-config file,
so its location must be passed in.  `tests/gen-data.sh` regenerates the
sample files with ffmpeg; they are committed, do not regenerate them
needlessly.

## Things to keep in mind

* The loader parses untrusted files.  Keep the size and memory limits, never
  trust sizes from the file, and run the tests under
  `-Db_sanitize=address,undefined` after touching the parser, the decoders
  or the converter.
* libvpx and dav1d are optional today but are going to become mandatory.  Do
  not add new `#ifdef` paths or options around them.
* Ask before making build-system, dependency or tooling decisions (the
  choice of meson was not asked about, and the author noticed).

## Commits

* Plain subject lines: no `[PATCH]`-style tags.
* Trailers (`Co-Authored-By:`, `Signed-off-by:`, `PR:`, ...) have exactly
  one TAB after the colon, never a space.  Check with
  `git log -1 --format=%B | cat -A`: the separator must show as `^I`.
* Name the model and the reasoning effort in `Co-Authored-By:`, and do not
  put an e-mail address there.
* Do not rewrite published history without being told to.
