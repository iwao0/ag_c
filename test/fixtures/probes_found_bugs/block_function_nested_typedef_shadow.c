int shared_function(void) {
  return 42;
}

int main(void) {
  extern int shared_function(void);
  {
    typedef int shared_function;
    shared_function value = 42;
    if (value != 42) return 1;
  }
  return shared_function() == 42 ? 0 : 2;
}
