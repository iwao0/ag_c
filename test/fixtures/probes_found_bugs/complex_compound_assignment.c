#include <assert.h>

static double double_real(double _Complex *value) {
  return ((double *)value)[0];
}

static double double_imaginary(double _Complex *value) {
  return ((double *)value)[1];
}

static float float_real(float _Complex *value) {
  return ((float *)value)[0];
}

static float float_imaginary(float _Complex *value) {
  return ((float *)value)[1];
}

struct complex_holder {
  double _Complex value;
};

struct signed_bits {
  signed int value : 5;
};

int main(void) {
  double _Complex values[2] = {{1.0, 2.0}, {3.0, 4.0}};
  int index = 0;
  values[index++] += (double _Complex){5.0, 6.0};
  assert(index == 1);
  assert(double_real(&values[0]) == 6.0);
  assert(double_imaginary(&values[0]) == 8.0);

  double _Complex expression_result =
      (values[0] -= (double _Complex){1.0, 3.0});
  assert(double_real(&values[0]) == 5.0);
  assert(double_imaginary(&values[0]) == 5.0);
  assert(double_real(&expression_result) == 5.0);
  assert(double_imaginary(&expression_result) == 5.0);

  double _Complex product = {1.0, 2.0};
  product *= (double _Complex){3.0, 4.0};
  assert(double_real(&product) == -5.0);
  assert(double_imaginary(&product) == 10.0);

  double _Complex quotient = {4.0, 2.0};
  quotient /= 2;
  assert(double_real(&quotient) == 2.0);
  assert(double_imaginary(&quotient) == 1.0);

  float _Complex narrow = {2.0f, 3.0f};
  narrow *= 2.0;
  narrow += (double _Complex){0.5, 1.5};
  assert(float_real(&narrow) == 4.5f);
  assert(float_imaginary(&narrow) == 7.5f);

  double scalar = 10.0;
  scalar += (float _Complex){2.5f, 7.0f};
  assert(scalar == 12.5);

  int integer = 10;
  integer += (double _Complex){2.75, 9.0};
  assert(integer == 12);

  _Bool boolean = 0;
  boolean += (double _Complex){0.0, 1.0};
  assert(boolean == 1);

  struct signed_bits bits = {3};
  bits.value += (float _Complex){4.5f, 8.0f};
  assert(bits.value == 7);

  struct complex_holder holder = {{8.0, 6.0}};
  struct complex_holder *holder_pointer = &holder;
  holder_pointer->value /= (double _Complex){2.0, 0.0};
  assert(double_real(&holder.value) == 4.0);
  assert(double_imaginary(&holder.value) == 3.0);
  return 0;
}
