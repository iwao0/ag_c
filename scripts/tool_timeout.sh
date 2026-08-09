#!/usr/bin/env bash

validate_tool_timeout_sec() {
  case "$1" in
    ''|0|*[!0-9]*)
      echo "invalid tool timeout seconds: $1" >&2
      return 1
      ;;
  esac
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
