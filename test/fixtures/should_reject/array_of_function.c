/* An array element type cannot be a function type. */
typedef int function_type(void);

function_type values[2];

int main(void) {
  return 0;
}
