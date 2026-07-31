struct packet {
  int values[6];
};

typedef struct packet plain_callback_function(void);
typedef volatile struct packet qualified_callback_function(void);
typedef int plain_consumer_function(plain_callback_function *callback);

static int consume_callback(qualified_callback_function *callback) {
  struct packet value = callback();
  return value.values[0];
}

int main(void) {
  plain_consumer_function *invalid = consume_callback;
  return invalid == 0;
}
