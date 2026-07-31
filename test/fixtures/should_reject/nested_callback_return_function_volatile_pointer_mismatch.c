typedef int target_function(int value);
typedef target_function *plain_factory_function(void);
typedef target_function *volatile qualified_factory_function(void);
typedef int plain_consumer_function(plain_factory_function *factory);

static int consume_factory(qualified_factory_function *factory) {
  return factory()(42);
}

int main(void) {
  plain_consumer_function *invalid = consume_factory;
  return invalid == 0;
}
