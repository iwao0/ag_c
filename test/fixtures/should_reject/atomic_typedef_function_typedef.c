/* A typedef cannot form an atomic-qualified function type. */
typedef int function_type(void);
typedef _Atomic function_type atomic_function_type;

int main(void) {
  return 0;
}
