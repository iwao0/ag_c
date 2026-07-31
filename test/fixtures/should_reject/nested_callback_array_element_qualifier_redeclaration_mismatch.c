typedef int plain_callback_function(int values[2]);
typedef int qualified_callback_function(const int values[2]);

int consume_callback(plain_callback_function *callback);

int consume_callback(qualified_callback_function *callback) {
  int values[2] = {19, 23};
  return callback(values);
}

int main(void) {
  return 0;
}
