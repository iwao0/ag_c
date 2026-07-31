struct pair {
  int left;
  int right;
};

typedef struct pair plain_callback_function(void);
typedef const struct pair qualified_callback_function(void);
typedef int plain_consumer_function(plain_callback_function *callback);

static int consume_callback(qualified_callback_function *callback) {
  struct pair value = callback();
  return value.left + value.right;
}

int main(void) {
  plain_consumer_function *invalid = consume_callback;
  return invalid == 0;
}
