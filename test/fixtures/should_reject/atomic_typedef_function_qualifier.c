/* The _Atomic qualifier cannot be applied to a function typedef. */
typedef int function_type(void);
_Atomic function_type function;

int main(void) {
  return 0;
}
