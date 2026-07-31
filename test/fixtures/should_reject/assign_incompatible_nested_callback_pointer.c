typedef int plain_callback_function(void);
typedef const int qualified_callback_function(void);
typedef int plain_consumer_function(plain_callback_function *callback);
typedef int qualified_consumer_function(
    qualified_callback_function *callback);

static int consume_qualified(qualified_callback_function *callback) {
  return callback();
}

int main(void) {
  plain_consumer_function *invalid = consume_qualified;
  return invalid == 0;
}
