#!/usr/bin/env bash
#
# Verify that every C++ and Python source file carries an SPDX license
# identifier near the top of the file. Consistent headers matter because this
# gem follows O3DE conventions, which require SPDX headers on source files.
#
# Usage: scripts/check_spdx_headers.sh
# Exit status is non-zero if any checked file is missing the identifier.

set -euo pipefail

cd "$(dirname "$0")/.."

expected='SPDX-License-Identifier: Apache-2.0 OR MIT'
status=0
checked=0

while IFS= read -r f; do
    case "$f" in
        */__pycache__/*) continue ;;
    esac
    checked=$((checked + 1))
    # Look only in the first 15 lines so a stray match deeper in a file does not
    # count as a header.
    if ! head -n 15 "$f" | grep -qF "$expected"; then
        echo "::error file=$f::missing or incorrect SPDX header (expected '$expected')"
        status=1
    fi
done < <(git ls-files 'Code/*.cpp' 'Code/*.h' 'Code/*.inl' '*.py')

echo "Checked $checked source file(s)."
if [ "$status" -eq 0 ]; then
    echo "All SPDX headers present."
fi
exit "$status"
