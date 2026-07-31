struct pair {
  int left;
  int right;
};

typedef struct pair plain_callback_function(void);
typedef const struct pair qualified_callback_function(void);

int consume_callback(plain_callback_function *callback);

int consume_callback(qualified_callback_function *callback) {
  struct pair value = callback();
  return value.left + value.right;
}

int main(void) {
  return 0;
}
