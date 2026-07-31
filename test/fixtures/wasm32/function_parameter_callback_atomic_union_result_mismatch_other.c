union result_words3 {
  unsigned int words[3];
  unsigned char bytes[12];
};

typedef union result_words3 plain_result_callback_function(void);

unsigned int consume_atomic_union_result(
    plain_result_callback_function *callback) {
  union result_words3 value = callback();
  return value.words[0] + value.words[1] + value.words[2];
}
