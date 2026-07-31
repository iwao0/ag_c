/* Default promotions make the nested unprototyped and int callback types compatible. */
typedef int unprototyped_callback_function();
typedef int prototyped_callback_function(int value);
typedef int unprototyped_consumer_function(
    unprototyped_callback_function *callback);
typedef int prototyped_consumer_function(
    prototyped_callback_function *callback);

int main(void) {
  return _Generic(
      (unprototyped_consumer_function *)0,
      unprototyped_consumer_function *: 1,
      prototyped_consumer_function *: 2,
      default: 0);
}
