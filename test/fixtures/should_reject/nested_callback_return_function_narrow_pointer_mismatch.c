typedef int unprototyped_target_function();
typedef int narrow_target_function(signed char value);
typedef unprototyped_target_function *unprototyped_factory_function(void);
typedef narrow_target_function *narrow_factory_function(void);

static int consume_factory(narrow_factory_function *factory) {
  return factory()(42);
}

int main(void) {
  int (*invalid)(unprototyped_factory_function *) = consume_factory;
  return invalid == 0;
}
