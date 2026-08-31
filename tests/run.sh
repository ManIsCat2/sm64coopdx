#!/bin/sh
# Runs the standalone save-location unit tests.
#
# The tests compile the production modules (src/pc/save_path.c,
# src/pc/save_location.c) against the stub headers in tests/stubs/. Because a
# quoted include first searches the directory of the including file,
# save_location.c normally picks up the real src/pc/configfile.h and
# src/pc/debuglog.h; compiling a copy of it from a scratch directory lets the
# stubs take precedence without touching the production sources.
set -e

repo=$(cd "$(dirname "$0")/.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT

cp "$repo/src/pc/save_location.c" "$build/save_location.c"

cc -std=gnu11 -Wall -Wextra -Werror -DSAVE_LOCATION_TEST \
   -I"$repo/tests/stubs" -I"$repo/src/pc" \
   -o "$build/test_save_location" \
   "$repo/tests/test_save_location.c" \
   "$repo/src/pc/save_path.c" \
   "$build/save_location.c"

"$build/test_save_location"

cc -std=gnu11 -Wall -Wextra -Werror -D_WIN32 \
   -I"$repo/src/pc" \
   -o "$build/test_save_path_windows" \
   "$repo/tests/test_save_path_windows.c" \
   "$repo/src/pc/save_path.c"

"$build/test_save_path_windows"

# Focused Windows-branch checks. The save/config temp-file code uses
# _S_IREAD/_S_IWRITE, which MinGW-w64 only defines in <sys/stat.h>; these
# checks fail if that include is dropped from either module. Skipped (with a
# note) where no MinGW-w64 cross toolchain is installed, so plain CI images
# without it still pass.
if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    mingw=x86_64-w64-mingw32-gcc

    # 1) full syntax check of save_location.c's Windows branch
    "$mingw" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only \
        -I"$repo/tests/stubs" -I"$repo/src/pc" "$build/save_location.c"

    # 2) compile probes of the guarded platform include blocks in both
    #    modules, extracted verbatim so the checks track the real sources
    for src in "$repo/src/pc/save_location.c" "$repo/src/pc/configfile.c"; do
        probe="$build/win_probe_$(basename "$src" .c).c"
        # extract the guarded platform include block verbatim (tracking nested
        # conditionals so the block stays balanced)
        awk '
            /^#if defined\(_WIN32\)$/ && !found { found = 1; depth = 1; print; next }
            found {
                print
                if ($0 ~ /^#if/) depth++
                if ($0 ~ /^#endif$/) { depth--; if (depth == 0) exit }
            }' "$src" > "$probe"
        printf '\nconst int win_probe_temp_macros = _S_IREAD | _S_IWRITE;\n' >> "$probe"
        "$mingw" -std=gnu11 -Wall -Werror -fsyntax-only "$probe"
    done
else
    echo "note: x86_64-w64-mingw32-gcc not found; skipping Windows-branch checks"
fi
