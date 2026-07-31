union qualified_result_word {
  unsigned int bits;
  float value;
};

typedef const _Atomic(union qualified_result_word)
    qualified_result_callback_function(void);

unsigned int consume_qualified_atomic_union_result(
    qualified_result_callback_function *callback);

static const _Atomic(union qualified_result_word)
produce_qualified_union(void) {
  return (union qualified_result_word){.bits = 42u};
}

int main(void) {
  return (int)consume_qualified_atomic_union_result(
      produce_qualified_union);
}
