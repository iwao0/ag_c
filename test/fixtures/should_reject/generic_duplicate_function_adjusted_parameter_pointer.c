typedef int unary_function(int);
typedef int function_parameter_function(unary_function callback);
typedef int pointer_parameter_function(unary_function *callback);

int main(void) {
  return _Generic(
      (function_parameter_function *)0,
      function_parameter_function *: 1,
      pointer_parameter_function *: 2,
      default: 0);
}
