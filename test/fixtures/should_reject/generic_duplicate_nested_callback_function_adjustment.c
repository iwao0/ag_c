typedef int unary_function(int value);
typedef int function_parameter_callback_function(unary_function transform);
typedef int function_pointer_callback_function(unary_function *transform);
typedef int function_parameter_consumer_function(
    function_parameter_callback_function *callback);
typedef int function_pointer_consumer_function(
    function_pointer_callback_function *callback);

int main(void) {
  return _Generic(
      (function_parameter_consumer_function *)0,
      function_parameter_consumer_function *: 1,
      function_pointer_consumer_function *: 2,
      default: 0);
}
