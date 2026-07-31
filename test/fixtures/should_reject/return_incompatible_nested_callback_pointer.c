/* A return expression preserves nested callback compatibility recursively. */
typedef int plain_callback_function(void);
typedef const int qualified_callback_function(void);
typedef int plain_consumer_function(plain_callback_function *callback);

static int consume_qualified(qualified_callback_function *callback) {
  return callback();
}

static plain_consumer_function *invalid_return(void) {
  return consume_qualified;
}

int main(void) {
  return invalid_return() == 0;
}
