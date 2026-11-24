#!/usr/bin/env bash
set -euo pipefail

# MSYS2 может передать длинную команду скрипту; native clang получает короткое имя response-файла.
: "${HOP_NDK_CLANG:?HOP_NDK_CLANG is required}"
: "${HOP_ANDROID_TARGET:?HOP_ANDROID_TARGET is required}"
if (( $# < 100 )); then
    exec "$HOP_NDK_CLANG" "--target=$HOP_ANDROID_TARGET" "$@"
fi

response=$(mktemp)
trap 'rm -f -- "$response"' EXIT
for argument in "$@"; do
    case "$argument" in
        -I/[a-zA-Z]/*) argument="-I$(cygpath -m "${argument:2}")" ;;
        -L/[a-zA-Z]/*) argument="-L$(cygpath -m "${argument:2}")" ;;
        /[a-zA-Z]/*) argument=$(cygpath -m "$argument") ;;
    esac
    argument=${argument//\\/\\\\}
    argument=${argument//\"/\\\"}
    printf '"%s"\n' "$argument" >> "$response"
done
"$HOP_NDK_CLANG" "--target=$HOP_ANDROID_TARGET" "@$(cygpath -m "$response")"
