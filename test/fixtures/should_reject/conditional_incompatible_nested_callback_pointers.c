/* A conditional expression rejects incompatible nested callback pointers. */
typedef int plain_callback_function(void);
typedef const int qualified_callback_function(void);

static int consume_plain(plain_callback_function *callback) {
  return callback();
}

static int consume_qualified(qualified_callback_function *callback) {
  return callback();
}

int main(void) {
  return (1 ? consume_plain : consume_qualified) != 0;
}
