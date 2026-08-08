int shared_object = 42;

static int parameter_shadow_boundary(int value) {
  {
    enum { value = 7 };
    if (value != 7) return 1;
  }
  return value == 42 ? 0 : 2;
}

int main(void) {
  extern int shared_object;
  {
    typedef int shared_object;
    shared_object value = 42;
    if (value != 42) return 3;
  }
  {
    enum { shared_object = 9 };
    if (shared_object != 9) return 4;
  }
  if (shared_object != 42) return 5;
  return parameter_shadow_boundary(shared_object);
}
