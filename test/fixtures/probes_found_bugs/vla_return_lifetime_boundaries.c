/*
 * Returning from a function while one or more VLAs are live must restore the
 * caller's dynamic stack. The return value must be materialized before that
 * restoration for scalar, complex, aggregate, and void return paths.
 */
#include <assert.h>
#include <complex.h>
#include <stddef.h>

#ifdef __wasm32__
#define LARGE_EXTENT_BASE (8 * 1024)
#define PASSES 20000
#else
#define LARGE_EXTENT_BASE (2 * 1024 * 1024)
#define PASSES 16
#endif

struct snapshot {
  size_t outer_bytes;
  size_t inner_bytes;
  unsigned first;
  unsigned last;
  int effects;
};

static unsigned void_checksum;

static int evaluate_bound(int *effects, int value) {
  (*effects)++;
  return value;
}

static void touch(
    volatile unsigned char *bytes, int extent, unsigned char seed) {
  bytes[0] = seed;
  bytes[extent - 1] = (unsigned char)(seed + 1);
  assert(bytes[0] == seed);
  assert(bytes[extent - 1] == (unsigned char)(seed + 1));
}

static size_t return_scalar_from_vla(int pass, int *observed_effects) {
  int effects = 0;
  int extent = LARGE_EXTENT_BASE + (pass & 15);
  volatile unsigned char bytes[evaluate_bound(&effects, extent)];
  unsigned char seed = (unsigned char)(pass + 3);
  touch(bytes, extent, seed);
  *observed_effects += effects;
  return sizeof(bytes) + bytes[0] + bytes[extent - 1];
}

static double _Complex return_complex_from_vla(
    int pass, int *observed_effects) {
  int effects = 0;
  int extent = LARGE_EXTENT_BASE + (pass & 15);
  volatile unsigned char bytes[evaluate_bound(&effects, extent)];
  unsigned char seed = (unsigned char)(pass + 7);
  touch(bytes, extent, seed);
  double real = (double)(pass & 127) + 0.25;
  double imag_part = -(double)(pass & 63) - 0.5;
  double _Complex result = real + imag_part * I;
  *observed_effects += effects;
  return result;
}

static struct snapshot return_aggregate_from_nested_vlas(
    int pass, int *observed_effects) {
  int effects = 0;
  int outer_extent = 1024 + (pass & 7);
  volatile unsigned char outer[
      evaluate_bound(&effects, outer_extent)];
  unsigned char outer_seed = (unsigned char)(pass + 11);
  touch(outer, outer_extent, outer_seed);

  {
    int inner_extent = LARGE_EXTENT_BASE + (pass & 15);
    volatile unsigned char inner[
        evaluate_bound(&effects, inner_extent)];
    unsigned char inner_seed = (unsigned char)(pass + 19);
    touch(inner, inner_extent, inner_seed);
    assert(outer[0] == outer_seed);
    assert(outer[outer_extent - 1] ==
           (unsigned char)(outer_seed + 1));

    struct snapshot result = {
        sizeof(outer),
        sizeof(inner),
        inner[0],
        inner[inner_extent - 1],
        effects,
    };
    *observed_effects += effects;
    return result;
  }
}

static void return_void_from_vla(int pass, int *observed_effects) {
  int effects = 0;
  int extent = LARGE_EXTENT_BASE + (pass & 15);
  volatile unsigned char bytes[evaluate_bound(&effects, extent)];
  unsigned char seed = (unsigned char)(pass + 29);
  touch(bytes, extent, seed);
  void_checksum += bytes[0] + bytes[extent - 1];
  *observed_effects += effects;
  if (pass & 1)
    return;
  return;
}

int main(void) {
  int scalar_effects = 0;
  for (int pass = 0; pass < PASSES; pass++) {
    size_t extent = LARGE_EXTENT_BASE + (pass & 15);
    unsigned char seed = (unsigned char)(pass + 3);
    assert(return_scalar_from_vla(pass, &scalar_effects) ==
           extent + seed + (unsigned char)(seed + 1));
  }
  assert(scalar_effects == PASSES);

  int complex_effects = 0;
  for (int pass = 0; pass < PASSES; pass++) {
    double _Complex result =
        return_complex_from_vla(pass, &complex_effects);
    assert(creal(result) == (double)(pass & 127) + 0.25);
    assert(cimag(result) == -(double)(pass & 63) - 0.5);
  }
  assert(complex_effects == PASSES);

  int aggregate_effects = 0;
  for (int pass = 0; pass < PASSES; pass++) {
    struct snapshot result =
        return_aggregate_from_nested_vlas(pass, &aggregate_effects);
    unsigned char seed = (unsigned char)(pass + 19);
    assert(result.outer_bytes == (size_t)(1024 + (pass & 7)));
    assert(result.inner_bytes ==
           (size_t)(LARGE_EXTENT_BASE + (pass & 15)));
    assert(result.first == (unsigned)seed);
    assert(result.last == (unsigned)(unsigned char)(seed + 1));
    assert(result.effects == 2);
  }
  assert(aggregate_effects == PASSES * 2);

  int void_effects = 0;
  unsigned expected_checksum = 0;
  for (int pass = 0; pass < PASSES; pass++) {
    unsigned char seed = (unsigned char)(pass + 29);
    expected_checksum += seed + (unsigned char)(seed + 1);
    return_void_from_vla(pass, &void_effects);
  }
  assert(void_effects == PASSES);
  assert(void_checksum == expected_checksum);
  return 0;
}
