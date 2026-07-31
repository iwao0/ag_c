typedef int *plain_callback_function(void);
typedef const int *qualified_callback_function(void);

static int consume_callback(qualified_callback_function *callback) {
  return *callback();
}

int main(void) {
  int (*invalid)(plain_callback_function *) = consume_callback;
  return invalid == 0;
}
