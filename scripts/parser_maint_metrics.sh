#!/usr/bin/env bash
set -euo pipefail

file="${1:-src/parser/parser.c}"
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

fn_metrics="$tmpdir/fn_metrics.tsv"
win_lines="$tmpdir/win_lines.txt"
dup_windows="$tmpdir/dup_windows.tsv"

awk '
function count_braces(s,    t, n1, n2) {
  t = s
  n1 = gsub(/\{/, "{", t)
  t = s
  n2 = gsub(/\}/, "}", t)
  return n1 - n2
}

function trim(s) {
  gsub(/^[ \t]+|[ \t]+$/, "", s)
  return s
}

BEGIN {
  in_func = 0
  fn = ""
  start = 0
  len = 0
  depth = 0
  branch = 0
}

{
  line = $0
  # normalize for duplicate-window scan
  norm = line
  sub(/\/\/.*/, "", norm)
  gsub(/[ \t]+/, " ", norm)
  norm = trim(norm)
  norm_lines[NR] = norm

  if (!in_func) {
    if (line ~ /^[A-Za-z_][A-Za-z0-9_ \t\*]*[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*\([^;]*\)[ \t]*\{/ &&
        line !~ /^[ \t]*(if|for|while|switch)[ \t]*\(/) {
      tmp = line
      sub(/\(.*/, "", tmp)
      gsub(/^[ \t]+|[ \t]+$/, "", tmp)
      n = split(tmp, a, /[ \t\*]+/)
      fn = (n > 0) ? a[n] : "(unknown)"
      start = NR
      len = 1
      branch = 0
      depth = count_braces(line)
      in_func = 1
      if (line ~ /\b(if|for|while|switch|case)\b/) branch++
    }
  } else {
    len++
    if (line ~ /\b(if|for|while|switch|case)\b/) branch++
    if (line ~ /\?/) branch++
    if (line ~ /&&/) branch++
    if (line ~ /\|\|/) branch++
    depth += count_braces(line)
    if (depth <= 0) {
      printf("%s\t%d\t%d\t%d\n", fn, start, len, branch)
      in_func = 0
      fn = ""
      start = 0
      len = 0
      depth = 0
      branch = 0
    }
  }
}

END {
  for (i = 1; i <= NR - 2; i++) {
    if (norm_lines[i] == "" || norm_lines[i+1] == "" || norm_lines[i+2] == "") continue
    key = norm_lines[i] " | " norm_lines[i+1] " | " norm_lines[i+2]
    print key > "'"$win_lines"'"
  }
}
' "$file" > "$fn_metrics"

sort "$win_lines" | uniq -c | awk '
{
  c = $1
  $1 = ""
  sub(/^[ \t]+/, "", $0)
  if (c > 1) printf("%d\t%s\n", c, $0)
}' > "$dup_windows"

echo "# Parser Maintainability Metrics"
echo
echo "- Target: \`$file\`"
echo "- Generated at: $(date '+%Y-%m-%d %H:%M:%S %z')"
echo

echo "## Summary"
total_funcs="$(wc -l < "$fn_metrics" | tr -d ' ')"
max_len="$(awk -F '\t' 'BEGIN{m=0} {if($3>m)m=$3} END{print m+0}' "$fn_metrics")"
avg_len="$(awk -F '\t' 'BEGIN{s=0;n=0} {s+=$3;n++} END{if(n==0)print "0.0"; else printf("%.1f", s/n)}' "$fn_metrics")"
max_branch="$(awk -F '\t' 'BEGIN{m=0} {if($4>m)m=$4} END{print m+0}' "$fn_metrics")"
avg_branch="$(awk -F '\t' 'BEGIN{s=0;n=0} {s+=$4;n++} END{if(n==0)print "0.0"; else printf("%.1f", s/n)}' "$fn_metrics")"
echo "- functions: $total_funcs"
echo "- max function length: $max_len lines"
echo "- avg function length: $avg_len lines"
echo "- max branch score: $max_branch"
echo "- avg branch score: $avg_branch"
echo

echo "## Top Functions by Length"
echo "| Function | Start Line | Lines | Branch Score |"
echo "|---|---:|---:|---:|"
sort -t $'\t' -k3,3nr "$fn_metrics" | head -n 10 | awk -F '\t' '{printf("| `%s` | %d | %d | %d |\n",$1,$2,$3,$4)}'
echo

echo "## Top Functions by Branch Score"
echo "| Function | Start Line | Branch Score | Lines |"
echo "|---|---:|---:|---:|"
sort -t $'\t' -k4,4nr "$fn_metrics" | head -n 10 | awk -F '\t' '{printf("| `%s` | %d | %d | %d |\n",$1,$2,$4,$3)}'
echo

echo "## Duplicate 3-line Windows (Heuristic)"
echo "| Count | Snippet |"
echo "|---:|---|"
if [[ -s "$dup_windows" ]]; then
  sort -t $'\t' -k1,1nr "$dup_windows" | head -n 10 | awk -F '\t' '{printf("| %d | `%s` |\n",$1,$2)}'
else
  echo "| 0 | (none detected) |"
fi
