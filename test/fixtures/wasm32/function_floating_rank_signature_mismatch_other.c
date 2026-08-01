// Incompatible definition: both the parameter and result use double instead
// of long double. The low-level Wasm type remains f64 -> f64.
double transform_floating_rank(double value) {
  return value + 0.25;
}
