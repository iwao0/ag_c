/* A parameter cannot have an atomic-qualified function type. */
typedef int function_type(void);

int apply(_Atomic function_type callback);

int main(void) {
  return 0;
}
