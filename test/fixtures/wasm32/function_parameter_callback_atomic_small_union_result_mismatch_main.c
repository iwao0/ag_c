union result_word1 {
  unsigned int bits;
  float value;
};

typedef _Atomic(union result_word1)
    atomic_result_callback_function(void);

unsigned int consume_atomic_small_union_result(
    atomic_result_callback_function *callback);

static _Atomic(union result_word1) produce_small_union(void) {
  return (union result_word1){.bits = 42u};
}

int main(void) {
  return (int)consume_atomic_small_union_result(produce_small_union);
}
