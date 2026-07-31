typedef int target_function(int value);
typedef target_function *plain_factory_function(void);
typedef _Atomic(target_function *) atomic_factory_function(void);
typedef int plain_consumer_function(plain_factory_function *factory);

static int consume_factory(atomic_factory_function *factory) {
  _Atomic(target_function *) callback = factory();
  return callback(42);
}

int main(void) {
  plain_consumer_function *invalid = consume_factory;
  return invalid == 0;
}
