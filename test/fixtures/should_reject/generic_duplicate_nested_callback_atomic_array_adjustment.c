/* Atomic array-declarator qualifiers are removed by nested parameter adjustment. */
typedef int atomic_array_callback_function(int values[_Atomic 1]);
typedef int pointer_callback_function(int *values);
typedef int atomic_consumer_function(
    atomic_array_callback_function *callback);
typedef int pointer_consumer_function(
    pointer_callback_function *callback);

int main(void) {
  return _Generic(
      (atomic_consumer_function *)0,
      atomic_consumer_function *: 1,
      pointer_consumer_function *: 2,
      default: 0);
}
