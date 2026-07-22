#include <assert.h>
#include <complex.h>

enum answer_code { ANSWER_CODE_VALUE = 42 };

static int round_to_int();
static int round_to_int(double value);
static int read_value();
static int read_value(const int *value);
static int enum_value();
static int enum_value(enum answer_code value);
static int complex_real();
static int complex_real(float _Complex value);

static int round_to_int(double value) {
  return (int)(value + 0.5);
}

static int read_value(const int *value) {
  return *value;
}

static int enum_value(enum answer_code value) {
  return value;
}

static int complex_real(float _Complex value) {
  return (int)__real__ value;
}

int main(void) {
  int value = 42;
  float _Complex complex_value = 42.0f + 1.0f * I;
  assert(round_to_int(41.5) == 42);
  assert(read_value(&value) == 42);
  assert(enum_value(ANSWER_CODE_VALUE) == 42);
  assert(complex_real(complex_value) == 42);
  return 0;
}
