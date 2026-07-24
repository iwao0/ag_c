/*
 * A block-scope extern declaration introduces a name only in that block.
 * The external entity may be defined later at file scope, and a nested
 * extern declaration may hide an automatic object without changing it.
 */
int read_later_object(void) {
  extern int later_object;
  return later_object;
}

int call_later_function(void) {
  extern int later_function(void);
  extern int later_function(void);
  return later_function();
}

int preserve_outer_automatic(void) {
  int later_object = 3;
  {
    extern int later_object;
    if (later_object != 11) return 1;
  }
  return later_object != 3;
}

int later_object = 11;

int later_function(void) {
  return 17;
}

int main(void) {
  if (read_later_object() != 11) return 1;
  if (call_later_function() != 17) return 2;
  if (preserve_outer_automatic() != 0) return 3;
  return 0;
}
