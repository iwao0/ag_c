typedef int array_callback_function(int values[const static 2]);
typedef int pointer_callback_function(int *values);
typedef int array_consumer_function(array_callback_function *callback);
typedef int pointer_consumer_function(pointer_callback_function *callback);

int main(void) {
  return _Generic(
      (array_consumer_function *)0,
      array_consumer_function *: 1,
      pointer_consumer_function *: 2,
      default: 0);
}
