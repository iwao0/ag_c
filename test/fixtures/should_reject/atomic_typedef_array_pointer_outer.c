typedef int array_type[2];
_Atomic array_type *pointer;

int main(void) {
  return pointer != 0;
}
