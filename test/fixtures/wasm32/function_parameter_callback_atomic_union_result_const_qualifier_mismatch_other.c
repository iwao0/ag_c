union qualified_result_word {
  unsigned int bits;
  float value;
};

typedef _Atomic(union qualified_result_word)
    unqualified_result_callback_function(void);

unsigned int consume_qualified_atomic_union_result(
    unqualified_result_callback_function *callback) {
  _Atomic(union qualified_result_word) value = callback();
  union qualified_result_word snapshot = value;
  return snapshot.bits;
}
