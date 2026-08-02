#!/usr/bin/env bash
# detect-quartus.sh — print the first locally-installed Quartus version.
#
# Usage: detect-quartus.sh VERSION [VERSION ...]
#   Checks, for each VERSION in order, whether a local install exists at
#   $ALTERA_LITE_ROOT/<VERSION>std/quartus/bin/quartus_sh.
#   Prints the first VERSION found and exits 0; exits 1 if none found.
#
# ALTERA_LITE_ROOT defaults to $HOME/tools/altera_lite.
set -euo pipefail

ALTERA_LITE_ROOT="${ALTERA_LITE_ROOT:-$HOME/tools/altera_lite}"

for ver in "$@"; do
    root="$ALTERA_LITE_ROOT/${ver}std"
    if [[ -x "$root/quartus/bin/quartus_sh" ]]; then
        printf '%s\n' "$ver"
        exit 0
    fi
done

exit 1
