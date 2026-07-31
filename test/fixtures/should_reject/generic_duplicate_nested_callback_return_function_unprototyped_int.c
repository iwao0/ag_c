/* Default promotions make these nested factory result function types compatible. */
typedef int unprototyped_target_function();
typedef int prototyped_target_function(int value);
typedef unprototyped_target_function *unprototyped_factory_function(void);
typedef prototyped_target_function *prototyped_factory_function(void);
typedef int unprototyped_consumer_function(
    unprototyped_factory_function *factory);
typedef int prototyped_consumer_function(
    prototyped_factory_function *factory);

int main(void) {
  return _Generic(
      (unprototyped_consumer_function *)0,
      unprototyped_consumer_function *: 1,
      prototyped_consumer_function *: 2,
      default: 0);
}
