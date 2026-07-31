typedef int target_function(int value);
typedef int plain_callback_function(target_function *target);
typedef int qualified_callback_function(target_function *const target);
typedef int plain_consumer_function(plain_callback_function *callback);
typedef int qualified_consumer_function(
    qualified_callback_function *callback);

int main(void) {
  return _Generic(
      (plain_consumer_function *)0,
      plain_consumer_function *: 1,
      qualified_consumer_function *: 2,
      default: 0);
}
