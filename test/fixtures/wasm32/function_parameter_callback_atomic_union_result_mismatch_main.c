union result_words3 {
  unsigned int words[3];
  unsigned char bytes[12];
};

typedef _Atomic(union result_words3)
    atomic_result_callback_function(void);

unsigned int consume_atomic_union_result(
    atomic_result_callback_function *callback);

static _Atomic(union result_words3) produce_union(void) {
  return (union result_words3){.words = {17, 13, 12}};
}

int main(void) {
  return (int)consume_atomic_union_result(produce_union);
}
