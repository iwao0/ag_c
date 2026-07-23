typedef int function_type(void);
_Atomic function_type *pointer;

int main(void) {
  return pointer != 0;
}
