#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
out_dir="$root/build/ag_wasm_link_smoke"

if ! command -v wasm-validate >/dev/null 2>&1 || ! command -v wasm-interp >/dev/null 2>&1; then
  echo "ag_wasm_link smoke: skip (wasm-validate/wasm-interp not found)"
  exit 0
fi

mkdir -p "$out_dir"

cat > "$out_dir/main.c" <<'SRC'
extern int other(int);
int main(void) { return other(40) + 2; }
SRC

cat > "$out_dir/other.c" <<'SRC'
int other(int x) { return x; }
SRC

cat > "$out_dir/gmain.c" <<'SRC'
extern int g;
int main(void) {
  g = g + 5;
  return g;
}
SRC

cat > "$out_dir/gother.c" <<'SRC'
int g = 37;
SRC

cat > "$out_dir/addr_main.c" <<'SRC'
extern int *p;
int main(void) { return *p + 2; }
SRC

cat > "$out_dir/addr_other.c" <<'SRC'
int g = 40;
int *p = &g;
SRC

cat > "$out_dir/static_main.c" <<'SRC'
int a(void);
int b(void);
int main(void) { return a() + b(); }
SRC

cat > "$out_dir/static_a.c" <<'SRC'
static int same(void) { return 20; }
int a(void) { return same(); }
SRC

cat > "$out_dir/static_b.c" <<'SRC'
static int same(void) { return 22; }
int b(void) { return same(); }
SRC

cat > "$out_dir/import_main.c" <<'SRC'
extern int host_add(int);
int main(void) { return host_add(41); }
SRC

{
  for i in $(seq 0 16999); do
    printf 'int g%d = %d;\n' "$i" "$i"
  done
  printf 'int main(void) { return g16999 - 16957; }\n'
} > "$out_dir/many_globals.c"

"$root/build/ag_c_wasm" -c -o "$out_dir/main.o" "$out_dir/main.c"
"$root/build/ag_c_wasm" -c -o "$out_dir/other.o" "$out_dir/other.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked.wasm" \
  "$out_dir/main.o" "$out_dir/other.o"
wasm-validate "$out_dir/linked.wasm"
wasm-interp "$out_dir/linked.wasm" --run-all-exports > "$out_dir/linked.interp"
grep -q 'main() => i32:42' "$out_dir/linked.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/gmain.o" "$out_dir/gmain.c"
"$root/build/ag_c_wasm" -c -o "$out_dir/gother.o" "$out_dir/gother.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_global.wasm" \
  "$out_dir/gmain.o" "$out_dir/gother.o"
wasm-validate "$out_dir/linked_global.wasm"
wasm-interp "$out_dir/linked_global.wasm" --run-all-exports > "$out_dir/linked_global.interp"
grep -q 'main() => i32:42' "$out_dir/linked_global.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/addr_main.o" "$out_dir/addr_main.c"
"$root/build/ag_c_wasm" -c -o "$out_dir/addr_other.o" "$out_dir/addr_other.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_addr.wasm" \
  "$out_dir/addr_main.o" "$out_dir/addr_other.o"
wasm-validate "$out_dir/linked_addr.wasm"
wasm-interp "$out_dir/linked_addr.wasm" --run-all-exports > "$out_dir/linked_addr.interp"
grep -q 'main() => i32:42' "$out_dir/linked_addr.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/static_main.o" "$out_dir/static_main.c"
"$root/build/ag_c_wasm" -c -o "$out_dir/static_a.o" "$out_dir/static_a.c"
"$root/build/ag_c_wasm" -c -o "$out_dir/static_b.o" "$out_dir/static_b.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_static.wasm" \
  "$out_dir/static_main.o" "$out_dir/static_a.o" "$out_dir/static_b.o"
wasm-validate "$out_dir/linked_static.wasm"
wasm-interp "$out_dir/linked_static.wasm" --run-all-exports > "$out_dir/linked_static.interp"
grep -q 'main() => i32:42' "$out_dir/linked_static.interp"

"$root/build/ag_c_wasm" -c -o "$out_dir/import_main.o" "$out_dir/import_main.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_import.wasm" \
  "$out_dir/import_main.o"
wasm-validate "$out_dir/linked_import.wasm"

"$root/build/ag_c_wasm" -c -o "$out_dir/many_globals.o" "$out_dir/many_globals.c"
"$root/build/ag_wasm_link" --no-entry --export=main -o "$out_dir/linked_many_globals.wasm" \
  "$out_dir/many_globals.o"
wasm-validate "$out_dir/linked_many_globals.wasm"
wasm-interp "$out_dir/linked_many_globals.wasm" --run-all-exports > "$out_dir/linked_many_globals.interp"
grep -q 'main() => i32:42' "$out_dir/linked_many_globals.interp"

echo "ag_wasm_link smoke: ok"
