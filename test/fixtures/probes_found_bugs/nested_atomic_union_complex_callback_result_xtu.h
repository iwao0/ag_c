#ifndef AG_C_NESTED_ATOMIC_UNION_COMPLEX_CALLBACK_RESULT_XTU_H
#define AG_C_NESTED_ATOMIC_UNION_COMPLEX_CALLBACK_RESULT_XTU_H

union result_words3 {
  unsigned int words[3];
  unsigned char bytes[12];
};

union result_word1 {
  unsigned int bits;
  float value;
};

typedef _Atomic(union result_word1)
    atomic_small_union_result_callback_function(void);
typedef _Atomic(union result_words3)
    atomic_union_result_callback_function(void);
typedef _Atomic(float _Complex)
    atomic_float_complex_result_callback_function(void);
typedef _Atomic(double _Complex)
    atomic_complex_result_callback_function(void);

unsigned int consume_atomic_small_union_result(
    atomic_small_union_result_callback_function *callback);
unsigned int consume_atomic_union_result(
    atomic_union_result_callback_function *callback);
unsigned int consume_atomic_float_complex_result(
    atomic_float_complex_result_callback_function *callback);
unsigned int consume_atomic_complex_result(
    atomic_complex_result_callback_function *callback);

#endif
