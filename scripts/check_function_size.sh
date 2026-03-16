#!/usr/bin/env bash
set -euo pipefail

max_lines="${MAX_FUNC_LINES:-180}"
files=("$@")
if [[ ${#files[@]} -eq 0 ]]; then
  files=(src/tokenizer/*.c)
fi

awk -v max_lines="$max_lines" '
function count_braces(s,    t, n1, n2) {
  t = s
  n1 = gsub(/\{/, "{", t)
  t = s
  n2 = gsub(/\}/, "}", t)
  return n1 - n2
}

BEGIN {
  in_func = 0
  failed = 0
}

{
  line = $0
  if (!in_func) {
    if (line ~ /^[A-Za-z_][A-Za-z0-9_ \t\*]*[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*\([^;]*\)[ \t]*\{/ &&
        line !~ /^[ \t]*(if|for|while|switch)[ \t]*\(/) {
      tmp = line
      sub(/\(.*/, "", tmp)
      gsub(/^[ \t]+|[ \t]+$/, "", tmp)
      n = split(tmp, a, /[ \t\*]+/)
      fn = (n > 0) ? a[n] : "(unknown)"
      file = FILENAME
      start = NR
      len = 1
      depth = count_braces(line)
      in_func = 1
    }
  } else {
    len++
    depth += count_braces(line)
    if (depth <= 0) {
      if (len > max_lines) {
        printf("function too long: %s (%s:%d) lines=%d > %d\n", fn, file, start, len, max_lines) > "/dev/stderr"
        failed = 1
      }
      in_func = 0
      fn = ""
      file = ""
      start = 0
      len = 0
      depth = 0
    }
  }
}

END {
  if (failed) exit 1
}
' "${files[@]}"

echo "function-size-check: OK (max_lines=${max_lines})"
