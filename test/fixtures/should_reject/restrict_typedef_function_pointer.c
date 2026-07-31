/* A function typedef does not make a restrict-qualified pointer valid. */
typedef int function_type(void);

int main(void) {
  function_type *restrict callback = 0;
  return callback != 0;
}
