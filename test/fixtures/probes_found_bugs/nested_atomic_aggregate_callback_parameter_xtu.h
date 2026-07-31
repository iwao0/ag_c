#ifndef AG_C_NESTED_ATOMIC_AGGREGATE_CALLBACK_PARAMETER_XTU_H
#define AG_C_NESTED_ATOMIC_AGGREGATE_CALLBACK_PARAMETER_XTU_H

struct words3 {
  unsigned int first;
  unsigned int second;
  unsigned int third;
};

typedef unsigned int atomic_words_callback_function(
    _Atomic(struct words3) value);

unsigned int inspect_atomic_words(
    _Atomic(struct words3) value);
unsigned int consume_atomic_words(
    atomic_words_callback_function *callback);

#endif
