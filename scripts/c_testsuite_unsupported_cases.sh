#!/usr/bin/env bash

# Strict-C exclusions shared by every c-testsuite runner. Keep these cases in
# one place so Native, Wasm WAT, Wasm object, and linked-object counts agree.
unsupported_reason() {
  case "$1" in
    00095) echo "function pointer to object pointer conversion" ;;
    00144) echo "discarding const qualifier in pointer assignment" ;;
    00206) echo "GNU #pragma push_macro/pop_macro" ;;
    00210) echo "GNU __attribute__ syntax and placement" ;;
    00213) echo "GNU statement expressions" ;;
    00214) echo "GNU statement expressions" ;;
    00216) echo "GNU empty struct / range designator" ;;
    *) return 1 ;;
  esac
}

validate_c_testsuite_timeout_sec() {
  case "$1" in
    ''|0|*[!0-9]*)
      echo "invalid c-testsuite timeout seconds: $1" >&2
      return 1
      ;;
  esac
}

c_testsuite_run_with_timeout() {
  local sec=$1
  shift
  perl -e '
    my $sec = shift @ARGV;
    my $pid = fork();
    die "fork failed: $!\n" unless defined $pid;
    if ($pid == 0) {
      exec @ARGV or die "exec failed: $!\n";
    }
    $SIG{ALRM} = sub {
      kill "TERM", $pid;
      select undef, undef, undef, 0.1;
      kill "KILL", $pid;
      waitpid($pid, 0);
      exit 124;
    };
    alarm $sec;
    waitpid($pid, 0);
    my $st = $?;
    exit(($st & 127) ? 128 + ($st & 127) : ($st >> 8));
  ' "$sec" "$@"
}

validate_c_testsuite_manifest() {
  local suite=$1
  local manifest=${BASH_SOURCE[0]}
  local unsupported_ids duplicate_ids id src expected

  unsupported_ids=$(sed -n \
    's/^[[:space:]]*\([0-9][0-9]*\)).*/\1/p' "$manifest")
  if [ -z "$unsupported_ids" ]; then
    echo "c-testsuite unsupported manifest is empty: $manifest" >&2
    return 1
  fi

  duplicate_ids=$(printf '%s\n' "$unsupported_ids" | LC_ALL=C sort | uniq -d)
  if [ -n "$duplicate_ids" ]; then
    echo "duplicate c-testsuite unsupported case IDs: $duplicate_ids" >&2
    return 1
  fi

  for id in $unsupported_ids; do
    case "$id" in
      [0-9][0-9][0-9][0-9][0-9]) ;;
      *)
        echo "invalid c-testsuite unsupported case ID: $id" >&2
        return 1
        ;;
    esac
    if [ ! -f "$suite/$id.c" ]; then
      echo "stale c-testsuite unsupported case ID: $id" >&2
      return 1
    fi
  done

  for src in "$suite"/[0-9]*.c; do
    [ -f "$src" ] || continue
    if [ ! -f "$src.expected" ]; then
      echo "missing c-testsuite expected output: $src.expected" >&2
      return 1
    fi
  done
  for expected in "$suite"/[0-9]*.c.expected; do
    [ -f "$expected" ] || continue
    src=${expected%.expected}
    if [ ! -f "$src" ]; then
      echo "stale c-testsuite expected output: $expected" >&2
      return 1
    fi
  done
}
