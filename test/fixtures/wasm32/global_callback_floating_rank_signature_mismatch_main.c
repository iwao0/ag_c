// A function-pointer data signature must recursively preserve floating rank.
typedef long double global_long_double_callback_t(long double value);

extern global_long_double_callback_t *global_floating_rank_callback;

int main(void) {
  return global_floating_rank_callback(5.25L) == 5.75L ? 0 : 1;
}
