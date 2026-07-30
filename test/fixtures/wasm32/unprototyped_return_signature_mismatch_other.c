// Paired with unprototyped_return_signature_mismatch_main.c.

struct actual_result {
  long long values[4];
};

struct actual_result build_result(int seed, double scale) {
  struct actual_result result = {
      {seed, (long long)scale, 0, 0},
  };
  return result;
}
