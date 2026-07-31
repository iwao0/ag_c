/* Restrict array-parameter qualifiers are removed by nested parameter adjustment. */
typedef int restrict_array_callback_function(
    int values[static const restrict 1]);
typedef int pointer_callback_function(int *values);
typedef int restrict_consumer_function(
    restrict_array_callback_function *callback);
typedef int pointer_consumer_function(
    pointer_callback_function *callback);

int main(void) {
  return _Generic(
      (restrict_consumer_function *)0,
      restrict_consumer_function *: 1,
      pointer_consumer_function *: 2,
      default: 0);
}
