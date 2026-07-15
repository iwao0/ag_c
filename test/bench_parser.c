#include "../src/parser/parser.h"
#include "../src/compilation_session.h"
#include "../src/frontend/translation_unit.h"
#include "../src/tokenizer/token.h"
#include "../src/tokenizer/tokenizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char *build_input_from_pattern(const char *pattern, size_t approx_bytes) {
  size_t pat_len = strlen(pattern);
  size_t cap = approx_bytes * 2 + pat_len + 1;
  char *buf = calloc(cap, 1);
  if (!buf) {
    fprintf(stderr, "failed to allocate benchmark input\n");
    exit(1);
  }
  size_t pos = 0;
  for (size_t i = 0; pos < approx_bytes; i++) {
    int written = snprintf(
        buf + pos, cap - pos, pattern, i, i, i);
    if (written < 0 || (size_t)written >= cap - pos) {
      fprintf(stderr, "failed to build benchmark input\n");
      free(buf);
      exit(1);
    }
    pos += (size_t)written;
  }
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

static void run_case(
    ag_compilation_session_t *session, const char *name,
    const char *pattern, size_t bytes) {
  char *input = build_input_from_pattern(pattern, bytes);

  struct timespec t_tok0;
  struct timespec t_tok1;
  struct timespec t_par0;
  struct timespec t_par1;

  clock_gettime(CLOCK_MONOTONIC, &t_tok0);
  tk_set_current_token(tk_tokenize(input));
  clock_gettime(CLOCK_MONOTONIC, &t_tok1);

  clock_gettime(CLOCK_MONOTONIC, &t_par0);
  node_t **code = psx_frontend_program_in_session(
      session, NULL, tk_get_current_token());
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
  ag_target_info_t target = ag_target_info_host();
  ag_compilation_session_t *session =
      ag_compilation_session_create(&target);
  if (!session || !ag_compilation_session_activate(session)) {
    ag_compilation_session_destroy(session);
    return 1;
  }
  const char *mixed_pattern =
      "int add_%zu(int a,int b){return a+b;}"
      "int mixed_%zu(void){int s=0;for(int i=0,j=8;i<j;i=i+1){if(i==3)continue;s=s+i;}return s;}\n";
  const char *expr_heavy_pattern =
      "int expr_%zu(int a,int b,int c){return ((a+b*3-c/2)%%7)<<2|((a&&b)||c);}"
      "int expr_call_%zu(void){return expr_%zu(10,20,30);}\n";
  const char *control_heavy_pattern =
      "int control_%zu(void){int i=0;int s=0;L:i=i+1;if(i<20){if(i==5)goto L;s=s+i;}return s;}\n";

  tk_set_strict_c11_mode(false);
  tk_set_enable_binary_literals(true);
  tk_set_enable_trigraphs(true);

  puts("Parser benchmark");
  run_case(session, "mixed", mixed_pattern, 16 * 1024);
  run_case(session, "mixed", mixed_pattern, 256 * 1024);
  run_case(session, "expr-heavy", expr_heavy_pattern, 256 * 1024);
  run_case(session, "control-heavy", control_heavy_pattern, 256 * 1024);
  ag_compilation_session_destroy(session);
  return 0;
}
