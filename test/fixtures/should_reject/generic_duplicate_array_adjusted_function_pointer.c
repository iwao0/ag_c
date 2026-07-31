/* Array parameters adjust to pointers, making these function associations compatible. */
typedef int array_parameter_function(int values[const static 1]);
typedef int pointer_parameter_function(int *values);

int main(void) {
  return _Generic(
      (array_parameter_function *)0,
      array_parameter_function *: 1,
      pointer_parameter_function *: 2,
      default: 0);
}
