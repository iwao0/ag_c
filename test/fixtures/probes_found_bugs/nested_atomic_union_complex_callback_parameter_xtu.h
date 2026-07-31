#ifndef AG_C_NESTED_ATOMIC_UNION_COMPLEX_CALLBACK_PARAMETER_XTU_H
#define AG_C_NESTED_ATOMIC_UNION_COMPLEX_CALLBACK_PARAMETER_XTU_H

union words3 {
  unsigned int words[3];
  unsigned char bytes[12];
};

union word1 {
  unsigned int bits;
  float value;
};

typedef unsigned int atomic_small_union_callback_function(
    const volatile _Atomic(union word1) value);
typedef unsigned int atomic_union_callback_function(
    const volatile _Atomic(union words3) value);
typedef unsigned int atomic_float_complex_callback_function(
    const volatile _Atomic(float _Complex) value);
typedef unsigned int atomic_complex_callback_function(
    const volatile _Atomic(double _Complex) value);

unsigned int inspect_atomic_small_union(
    const volatile _Atomic(union word1) value);
unsigned int inspect_atomic_union(
    const volatile _Atomic(union words3) value);
unsigned int inspect_atomic_float_complex(
    const volatile _Atomic(float _Complex) value);
unsigned int inspect_atomic_complex(
    const volatile _Atomic(double _Complex) value);
unsigned int consume_atomic_small_union(
    atomic_small_union_callback_function *callback);
unsigned int consume_atomic_union(
    atomic_union_callback_function *callback);
unsigned int consume_atomic_float_complex(
    atomic_float_complex_callback_function *callback);
unsigned int consume_atomic_complex(
    atomic_complex_callback_function *callback);

#endif
