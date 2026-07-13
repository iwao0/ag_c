#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int mkdir_p(const char *path) {
  char tmp[PATH_MAX];
  size_t len = strlen(path);
  if (len == 0 || len >= sizeof(tmp)) return -1;
  memcpy(tmp, path, len + 1);
  for (char *p = tmp + 1; *p; p++) {
    if (*p != '/') continue;
    *p = '\0';
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    *p = '/';
  }
  return mkdir(tmp, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

static int write_source_file(const char *path, const char *source) {
  FILE *fp = fopen(path, "w");
  if (!fp) return -1;
  int failed = fputs(source, fp) == EOF || fclose(fp) != 0;
  return failed ? -1 : 0;
}

static int file_contains(const char *path, const char *needle, size_t max_len) {
  FILE *fp = fopen(path, "r");
  if (!fp) return 0;
  char buf[2048];
  size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
  int too_long = fgetc(fp) != EOF || n > max_len;
  fclose(fp);
  buf[n] = '\0';
  return !too_long && strstr(buf, needle) != NULL;
}

static int run_failure_with_limits(const char *source_path, const char *log_path,
                                   const char *expected, rlim_t nofile_limit,
                                   rlim_t memory_limit) {
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen(log_path, "w", stderr);
    if (nofile_limit > 0) {
      struct rlimit limit = {nofile_limit, nofile_limit};
      if (setrlimit(RLIMIT_NOFILE, &limit) != 0) _exit(125);
    }
    if (memory_limit > 0) {
#ifdef RLIMIT_AS
      struct rlimit limit = {memory_limit, memory_limit};
      if (setrlimit(RLIMIT_AS, &limit) != 0) _exit(125);
#elif defined(RLIMIT_DATA)
      struct rlimit limit = {memory_limit, memory_limit};
      if (setrlimit(RLIMIT_DATA, &limit) != 0) _exit(125);
#else
      _exit(125);
#endif
    }
    execl("./build/ag_c", "./build/ag_c", source_path, (char *)NULL);
    _exit(126);
  }
  if (pid < 0) return -1;
  int status = 0;
  if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 1) return -1;
  return file_contains(log_path, expected, 1024) ? 0 : -1;
}

int main(void) {
  const char *dir = "build/e2e/sandbox";
  const char *semantic_source = "int main() { const int x = 5; x = 10; return 0; }\n";
  if (mkdir_p(dir) != 0) return 1;

  const char *blocked = "build/e2e/sandbox/no_read_input.c";
  const char *blocked_log = "build/e2e/logs/sandbox_no_read_input.log";
  if (write_source_file(blocked, "int main(){return 0;}\n") != 0 || chmod(blocked, 0000) != 0) return 1;
  int blocked_rc = run_failure_with_limits(
      blocked, blocked_log, "入力ファイルを読み込めませんでした", 0, 0);
  (void)chmod(blocked, 0644);
  if (blocked_rc != 0) {
    fprintf(stderr, "Sandbox fixture failed: no_read_input (see %s)\n", blocked_log);
    return 1;
  }

  const char *fd_source = "build/e2e/sandbox/fd_limit_input.c";
  const char *fd_log = "build/e2e/logs/sandbox_fd_limit.log";
  if (write_source_file(fd_source, semantic_source) != 0 ||
      run_failure_with_limits(fd_source, fd_log, "E3077", 32, 0) != 0) {
    fprintf(stderr, "Sandbox fixture failed: fd_limit_input (see %s)\n", fd_log);
    return 1;
  }

#ifdef __APPLE__
  /* RLIMIT_AS also constrains dyld mappings on macOS, so a low limit can
   * reject exec before ag_c starts. That tests the host loader, not ag_c. */
  puts("Native E2E sandbox fixture skipped: mem_limit_input (macOS RLIMIT_AS)");
#else
  const char *mem_source = "build/e2e/sandbox/mem_limit_input.c";
  const char *mem_log = "build/e2e/logs/sandbox_mem_limit.log";
  if (write_source_file(mem_source, semantic_source) != 0 ||
      run_failure_with_limits(mem_source, mem_log, "E3077", 0, 64 * 1024 * 1024) != 0) {
    fprintf(stderr, "Sandbox fixture failed: mem_limit_input (see %s)\n", mem_log);
    return 1;
  }
#endif

  puts("Native E2E sandbox fixtures: ok");
  return 0;
}
