typedef int plain_callback_function(void);
typedef const int qualified_callback_function(void);
typedef int plain_consumer_function(plain_callback_function *callback);

static int consume_qualified(qualified_callback_function *callback) {
  return callback();
}

static int apply_consumer(
    plain_consumer_function *consumer,
    plain_callback_function *callback) {
  return consumer(callback);
}

static int plain_callback(void) {
  return 42;
}

int main(void) {
  return apply_consumer(consume_qualified, plain_callback) != 42;
}
