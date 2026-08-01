// Incompatible definition: the callback uses double instead of long double,
// although both callback types lower to the same Wasm function type.
typedef double global_double_callback_t(double value);

static double add_floating_rank_fraction(double value) {
  return value + 0.5;
}

global_double_callback_t *global_floating_rank_callback =
    add_floating_rank_fraction;
