/* A local declaration cannot apply _Atomic to a function typedef. */
typedef int function_type(void);

int main(void) {
  _Atomic function_type function;
  return 0;
}
