#include "../src/tokenizer/internal/keywords.h"
#include "../src/tokenizer/token.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

token_kind_t lookup_keyword_gperf(const char *s, int len);

typedef token_kind_t (*lookup_fn_t)(const char *s, int len);

static double elapsed_sec(struct timespec start, struct timespec end) {
  double s = (double)(end.tv_sec - start.tv_sec);
  double ns = (double)(end.tv_nsec - start.tv_nsec) / 1000000000.0;
  return s + ns;
}

static void run_case(const char *name, lookup_fn_t fn, const char **words, int n_words, int rounds) {
  struct timespec t0;
  struct timespec t1;
  volatile unsigned long long sink = 0;

  clock_gettime(CLOCK_MONOTONIC, &t0);
  for (int r = 0; r < rounds; r++) {
    for (int i = 0; i < n_words; i++) {
      sink += (unsigned long long)fn(words[i], (int)strlen(words[i]));
    }
  }
  clock_gettime(CLOCK_MONOTONIC, &t1);

  double sec = elapsed_sec(t0, t1);
  double qps = sec > 0.0 ? (double)n_words * (double)rounds / sec : 0.0;
  printf("keyword-bench case=%s qps=%.0f elapsed=%.6f sink=%llu\n", name, qps, sec, sink);
}

int main(void) {
  static const char *mixed_words[] = {
      "if",          "foo",      "while",      "bar",     "return",  "baz",
      "_Alignas",    "_Thread_local", "int",   "alpha",   "static",  "omega",
      "_Static_assert", "continue", "register", "hello", "world",   "unsigned",
      "double",      "volatile", "enum",       "kappa",   "lambda",  "_Imaginary",
  };
  static const int rounds = 300000;

  run_case("manual", lookup_keyword, mixed_words, (int)(sizeof(mixed_words) / sizeof(mixed_words[0])), rounds);
  run_case("gperf", lookup_keyword_gperf, mixed_words, (int)(sizeof(mixed_words) / sizeof(mixed_words[0])), rounds);
  return 0;
}
