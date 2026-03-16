#include "../src/tokenizer/tokenizer.h"
#include "../src/tokenizer/token.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static size_t count_tokens(token_t *tok) {
  size_t n = 0;
  for (; tok; tok = tok->next) {
    n++;
    if (tok->kind == TK_EOF) break;
  }
  return n;
}

static char *build_input(size_t approx_bytes) {
  const char *pattern =
      "int main(){int x=0;for(int i=0;i<100;i++){x+=i;}if(x>=10){x=x-1;}return x;}\n";
  size_t pat_len = strlen(pattern);
  size_t n = approx_bytes / pat_len + 1;
  char *buf = calloc(n * pat_len + 1, 1);
  if (!buf) {
    fprintf(stderr, "failed to allocate benchmark input\n");
    exit(1);
  }
  char *p = buf;
  for (size_t i = 0; i < n; i++) {
    memcpy(p, pattern, pat_len);
    p += pat_len;
  }
  *p = '\0';
  return buf;
}

static double elapsed_sec(struct timespec start, struct timespec end) {
  double s = (double)(end.tv_sec - start.tv_sec);
  double ns = (double)(end.tv_nsec - start.tv_nsec) / 1000000000.0;
  return s + ns;
}

static void run_case(size_t bytes) {
  char *input = build_input(bytes);
  struct timespec t0;
  struct timespec t1;

  reset_tokenizer_stats();
  clock_gettime(CLOCK_MONOTONIC, &t0);
  token_t *tok = tokenize(input);
  clock_gettime(CLOCK_MONOTONIC, &t1);

  tokenizer_stats_t st = get_tokenizer_stats();
  size_t token_count = count_tokens(tok);
  double sec = elapsed_sec(t0, t1);
  double tps = sec > 0.0 ? token_count / sec : 0.0;

  printf("input=%zub tokens=%zu time=%.6fs tokens/sec=%.0f alloc_count=%zu peak_alloc_bytes=%zu\n",
         strlen(input), token_count, sec, tps, st.alloc_count, st.peak_alloc_bytes);
  free(input);
}

int main(void) {
  set_strict_c11_mode(false);
  set_enable_binary_literals(true);
  set_enable_trigraphs(true);

  puts("Tokenizer benchmark");
  run_case(1024);
  run_case(16 * 1024);
  run_case(256 * 1024);
  return 0;
}
