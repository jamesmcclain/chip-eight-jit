#!/usr/bin/env bash
# Format C and C++ sources in GNU style (200 columns). Keep declaration types
# with their names: this is stable for the repository's C++ source.
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

format_file() {
    local file=$1 tmp

    tmp=$(mktemp "${file}.indent.XXXXXX")
    trap 'rm -f "$tmp"' RETURN
    indent -npro -gnu -npsl -l200 -o "$tmp" "$file"
    chmod --reference="$file" "$tmp"
    mv -- "$tmp" "$file"
    trap - RETURN
}

while IFS= read -r -d '' file; do
    format_file "$file"
done < <(
    find src tests tools -type f \( \
        -name '*.c' -o -name '*.h' -o \
        -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' -o \
        -name '*.hh' -o -name '*.hpp' -o -name '*.hxx' \
    \) -print0
)
