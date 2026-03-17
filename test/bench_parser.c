#include "../src/parser/parser.h"
#include "../src/tokenizer/token.h"
#include "../src/tokenizer/tokenizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char *build_input_from_pattern(const char *pattern, size_t approx_bytes) {
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

static size_t count_funcs(node_t **nodes) {
  size_t n = 0;
  if (!nodes) return 0;
  while (nodes[n]) n++;
  return n;
}

static void run_case(const char *name, const char *pattern, size_t bytes) {
  char *input = build_input_from_pattern(pattern, bytes);

  struct timespec t_tok0;
  struct timespec t_tok1;
  struct timespec t_par0;
  struct timespec t_par1;

  clock_gettime(CLOCK_MONOTONIC, &t_tok0);
  token = tk_tokenize(input);
  clock_gettime(CLOCK_MONOTONIC, &t_tok1);

  clock_gettime(CLOCK_MONOTONIC, &t_par0);
  node_t **code = ps_program();
  clock_gettime(CLOCK_MONOTONIC, &t_par1);

  size_t funcs = count_funcs(code);
  double tok_sec = elapsed_sec(t_tok0, t_tok1);
  double par_sec = elapsed_sec(t_par0, t_par1);
  double par_mbps = par_sec > 0.0 ? ((double)strlen(input) / (1024.0 * 1024.0)) / par_sec : 0.0;
  double fps = par_sec > 0.0 ? (double)funcs / par_sec : 0.0;

  printf("case=%s input=%zub funcs=%zu tokenize=%.6fs parser=%.6fs parser_MB/s=%.2f funcs/sec=%.0f\n",
         name, strlen(input), funcs, tok_sec, par_sec, par_mbps, fps);
  free(input);
}

int main(void) {
  const char *mixed_pattern =
      "int add(int a,int b){return a+b;}"
      "int main(){int s=0;for(int i=0,j=8;i<j;i=i+1){if(i==3)continue;s=s+i;}return s;}\n";
  const char *expr_heavy_pattern =
      "int f(int a,int b,int c){return ((a+b*3-c/2)%7)<<2|((a&&b)||c);}"
      "int main(){return f(10,20,30);}\n";
  const char *control_heavy_pattern =
      "int main(){int i=0;int s=0;L:i=i+1;if(i<20){if(i==5)goto L;s=s+i;}return s;}\n";

  tk_set_strict_c11_mode(false);
  tk_set_enable_binary_literals(true);
  tk_set_enable_trigraphs(true);

  puts("Parser benchmark");
  run_case("mixed", mixed_pattern, 16 * 1024);
  run_case("mixed", mixed_pattern, 256 * 1024);
  run_case("expr-heavy", expr_heavy_pattern, 256 * 1024);
  run_case("control-heavy", control_heavy_pattern, 256 * 1024);
  return 0;
}
