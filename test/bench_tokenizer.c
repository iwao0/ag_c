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

static char *read_file_all(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    fprintf(stderr, "failed to open file: %s\n", path);
    exit(1);
  }
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    fprintf(stderr, "failed to seek file: %s\n", path);
    exit(1);
  }
  long sz = ftell(fp);
  if (sz < 0) {
    fclose(fp);
    fprintf(stderr, "failed to tell file size: %s\n", path);
    exit(1);
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    fprintf(stderr, "failed to rewind file: %s\n", path);
    exit(1);
  }
  char *buf = calloc((size_t)sz + 1, 1);
  if (!buf) {
    fclose(fp);
    fprintf(stderr, "failed to allocate file buffer: %s\n", path);
    exit(1);
  }
  size_t nread = fread(buf, 1, (size_t)sz, fp);
  fclose(fp);
  if (nread != (size_t)sz) {
    free(buf);
    fprintf(stderr, "failed to read file: %s\n", path);
    exit(1);
  }
  buf[nread] = '\0';
  return buf;
}

static double elapsed_sec(struct timespec start, struct timespec end) {
  double s = (double)(end.tv_sec - start.tv_sec);
  double ns = (double)(end.tv_nsec - start.tv_nsec) / 1000000000.0;
  return s + ns;
}

static void run_case_with_input(const char *name, char *input) {
  struct timespec t0;
  struct timespec t1;

  tk_reset_tokenizer_stats();
  clock_gettime(CLOCK_MONOTONIC, &t0);
  token_t *tok = tk_tokenize(input);
  clock_gettime(CLOCK_MONOTONIC, &t1);

  tokenizer_stats_t st = tk_get_tokenizer_stats();
  size_t token_count = count_tokens(tok);
  double sec = elapsed_sec(t0, t1);
  double tps = sec > 0.0 ? token_count / sec : 0.0;

  printf("case=%s input=%zub tokens=%zu time=%.6fs tokens/sec=%.0f alloc_count=%zu peak_alloc_bytes=%zu\n",
         name, strlen(input), token_count, sec, tps, st.alloc_count, st.peak_alloc_bytes);
}

static void run_case(const char *name, const char *pattern, size_t bytes) {
  char *input = build_input_from_pattern(pattern, bytes);
  run_case_with_input(name, input);
  free(input);
}

int main(void) {
  const char *mixed_pattern =
      "int main(){int x=0;for(int i=0;i<100;i++){x+=i;}if(x>=10){x=x-1;}return x;}\n";
  const char *ident_pattern =
      "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron\n";
  const char *numeric_pattern =
      "0 1 2 3 4 5 6 7 8 9 10 11 12 255 1024 65535 3.14 6.02e23 0x1.fp+3 0777 0b101010\n";
  const char *punct_pattern =
      "{ } ( ) [ ] ; , . ... + - * / % ++ -- += -= *= /= %= == != < <= > >= && || & | ^ ~ ? : -> << >> <<= >>= # ## %: %:%: <::> <% %>\n";

  tk_set_strict_c11_mode(false);
  tk_set_enable_binary_literals(true);
  tk_set_enable_trigraphs(true);

  puts("Tokenizer benchmark");
  run_case("mixed", mixed_pattern, 1024);
  run_case("mixed", mixed_pattern, 16 * 1024);
  run_case("mixed", mixed_pattern, 256 * 1024);
  run_case("ident", ident_pattern, 256 * 1024);
  run_case("numeric", numeric_pattern, 256 * 1024);
  run_case("punct", punct_pattern, 256 * 1024);

  const char *corpus = getenv("TOKENIZER_BENCH_CORPUS_FILE");
  if (corpus && corpus[0] != '\0') {
    char *content = read_file_all(corpus);
    run_case_with_input("corpus", content);
    free(content);
  }
  return 0;
}
