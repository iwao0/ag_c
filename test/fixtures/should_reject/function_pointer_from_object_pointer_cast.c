/*
 * A zero integer cast to an object pointer type is a null pointer value, but
 * not a null pointer constant. It cannot initialize a function pointer.
 * Expect ag_c E3099.
 */
typedef int callback_type(void);

int main(void) {
  callback_type *callback = (int *)0;
  return callback != 0;
}
