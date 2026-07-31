union result_word1 {
  unsigned int bits;
  float value;
};

typedef union result_word1 plain_result_callback_function(void);

unsigned int consume_atomic_small_union_result(
    plain_result_callback_function *callback) {
  return callback().bits;
}
