typedef int plain_callback_function(void);
typedef const int qualified_callback_function(void);

int consume_callback(plain_callback_function *callback);

int consume_callback(qualified_callback_function *callback) {
  return callback();
}

int main(void) {
  return 0;
}
