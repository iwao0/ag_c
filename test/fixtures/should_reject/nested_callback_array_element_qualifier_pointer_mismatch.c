typedef int plain_callback_function(int values[2]);
typedef int qualified_callback_function(const int values[2]);

static int consume_callback(qualified_callback_function *callback) {
  int values[2] = {19, 23};
  return callback(values);
}

int main(void) {
  int (*invalid)(plain_callback_function *) = consume_callback;
  return invalid == 0;
}
