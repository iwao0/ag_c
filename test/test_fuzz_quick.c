#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
  unsigned int state;
} prng_t;

static unsigned int prng_next(prng_t *r) {
  r->state = r->state * 1664525u + 1013904223u;
  return r->state;
}

static int mkdir_p(const char *path) {
  char tmp[512];
  size_t len = strlen(path);
  if (len == 0 || len >= sizeof(tmp)) return -1;
  memcpy(tmp, path, len + 1);
  if (tmp[len - 1] == '/') tmp[len - 1] = '\0';
  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return -1;
      *p = '/';
    }
  }
  if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return -1;
  return 0;
}

static int run_ag_c_with_timeout(const char *src, int timeout_ms) {
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    execl("./build/ag_c", "./build/ag_c", src, (char *)NULL);
    _exit(127);
  }
  if (pid < 0) return -1;

  int waited = 0;
  int status = 0;
  while (waited < timeout_ms) {
    pid_t w = waitpid(pid, &status, WNOHANG);
    if (w == pid) {
      if (!WIFEXITED(status)) return -1;
      int ec = WEXITSTATUS(status);
      return (ec == 0 || ec == 1) ? 0 : -1;
    }
    if (w < 0) return -1;
    usleep(1000 * 10);
    waited += 10;
  }
  kill(pid, SIGKILL);
  waitpid(pid, &status, 0);
  return -1;
}

static void append_token(char *buf, size_t cap, size_t *len, const char *tok) {
  size_t n = strlen(tok);
  if (*len + n + 1 >= cap) return;
  memcpy(buf + *len, tok, n);
  *len += n;
  buf[*len] = '\0';
}

static void generate_if_macro_case(prng_t *r, char *out, size_t cap) {
  static const char *idents[] = {"A", "B", "X", "Y", "Z", "F", "G"};
  static const char *ops[] = {" + ", " - ", " * ", " && ", " || ", " == ", " != "};
  size_t len = 0;
  append_token(out, cap, &len, "#define A 1\n#define B 0\n#if ");
  int terms = 8 + (int)(prng_next(r) % 48);
  for (int i = 0; i < terms; i++) {
    if ((prng_next(r) & 1u) == 0) {
      char num[16];
      snprintf(num, sizeof(num), "%u", prng_next(r) % 7u);
      append_token(out, cap, &len, num);
    } else {
      append_token(out, cap, &len, idents[prng_next(r) % (sizeof(idents) / sizeof(idents[0]))]);
    }
    if (i + 1 < terms) append_token(out, cap, &len, ops[prng_next(r) % (sizeof(ops) / sizeof(ops[0]))]);
  }
  append_token(out, cap, &len, "\nint main(){return 0;}\n#else\nint main(){return 1;}\n#endif\n");
}

static void generate_initializer_case(prng_t *r, char *out, size_t cap) {
  size_t len = 0;
  int n = 2 + (int)(prng_next(r) % 24);
  append_token(out, cap, &len, "int main(){ int a[32]={");
  for (int i = 0; i < n; i++) {
    char num[16];
    snprintf(num, sizeof(num), "%u", prng_next(r) % 100u);
    append_token(out, cap, &len, num);
    if (i + 1 < n) append_token(out, cap, &len, ",");
  }
  append_token(out, cap, &len, "}; return a[0]; }\n");
}

static void generate_include_case(prng_t *r, char *out, size_t cap) {
  static const char *paths[] = {
      "build/not_found.h",
      "build/./not_found.h",
      "build//not_found.h",
      "../README.md",
      "/tmp/blocked_absolute_path.h",
  };
  size_t len = 0;
  append_token(out, cap, &len, "#include \"");
  append_token(out, cap, &len, paths[prng_next(r) % (sizeof(paths) / sizeof(paths[0]))]);
  append_token(out, cap, &len, "\"\nint main(){return 0;}\n");
}

int main(void) {
  const int cases = 120;
  const int timeout_ms = 250;
  printf("Running quick fuzz smoke...\n");

  if (mkdir_p("build/fuzz/quick") != 0) {
    fprintf(stderr, "FAIL: cannot create fuzz directory\n");
    return 1;
  }

  prng_t rng = {0xC0FFEEu};
  for (int i = 0; i < cases; i++) {
    char src[256];
    snprintf(src, sizeof(src), "build/fuzz/quick/case_%03d.c", i);
    FILE *fp = fopen(src, "w");
    if (!fp) {
      fprintf(stderr, "FAIL: cannot create %s\n", src);
      return 1;
    }

    char buf[4096];
    buf[0] = '\0';
    switch (prng_next(&rng) % 3u) {
      case 0: generate_if_macro_case(&rng, buf, sizeof(buf)); break;
      case 1: generate_initializer_case(&rng, buf, sizeof(buf)); break;
      default: generate_include_case(&rng, buf, sizeof(buf)); break;
    }
    fputs(buf, fp);
    fclose(fp);

    if (run_ag_c_with_timeout(src, timeout_ms) != 0) {
      fprintf(stderr, "FAIL: fuzz case crashed/hung: %s\n", src);
      return 1;
    }
  }

  printf("OK: quick fuzz smoke passed (%d cases)\n", cases);
  return 0;
}
