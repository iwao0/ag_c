/* A pointer declarator cannot make an invalid atomic array base type valid. */
typedef int array_type[2];
_Atomic array_type *pointer;

int main(void) {
  return pointer != 0;
}
