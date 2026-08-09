#!/usr/bin/env bash

validate_tool_timeout_sec() {
  case "$1" in
    ''|0|*[!0-9]*)
      echo "invalid tool timeout seconds: $1" >&2
      return 1
      ;;
  esac
}

acquire_lock_dir() {
  local lock_dir=$1
  local sec=$2
  local attempts

  validate_tool_timeout_sec "$sec" || return 1
  attempts=$((sec * 10))
  while ! mkdir "$lock_dir" 2>/dev/null; do
    if [ "$attempts" -le 0 ]; then
      echo "timed out waiting for lock: $lock_dir" >&2
      return 124
    fi
    sleep 0.1
    attempts=$((attempts - 1))
  done
}

run_with_timeout() {
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
