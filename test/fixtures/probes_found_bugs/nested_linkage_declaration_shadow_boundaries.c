int external_object = 42;

int external_function(void) {
  return 42;
}

static int automatic_shadow_boundaries(void) {
  int external_object = 7;
  int external_function = 8;
  {
    extern int external_object;
    if (external_object != 42) return 1;
  }
  {
    extern int external_function(void);
    if (external_function() != 42) return 2;
  }
  return external_object == 7 && external_function == 8 ? 0 : 3;
}

static int typedef_shadow_boundaries(void) {
  typedef int external_object;
  typedef int external_function;
  {
    extern int external_object;
    if (external_object != 42) return 4;
  }
  {
    extern int external_function(void);
    if (external_function() != 42) return 5;
  }
  external_object object_value = 7;
  external_function function_value = 8;
  return object_value == 7 && function_value == 8 ? 0 : 6;
}

static int enumerator_shadow_boundaries(void) {
  enum { external_object = 7, external_function = 8 };
  {
    extern int external_object;
    if (external_object != 42) return 7;
  }
  {
    extern int external_function(void);
    if (external_function() != 42) return 8;
  }
  return external_object == 7 && external_function == 8 ? 0 : 9;
}

int main(void) {
  int result = automatic_shadow_boundaries();
  if (result != 0) return result;
  result = typedef_shadow_boundaries();
  if (result != 0) return result;
  return enumerator_shadow_boundaries();
}
