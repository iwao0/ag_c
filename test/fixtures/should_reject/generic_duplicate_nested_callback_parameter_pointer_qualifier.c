typedef int callback_function(void);
typedef int plain_consumer_function(callback_function *callback);
typedef int qualified_consumer_function(callback_function *const callback);

int main(void) {
  return _Generic(
      (plain_consumer_function *)0,
      plain_consumer_function *: 1,
      qualified_consumer_function *: 2,
      default: 0);
}
