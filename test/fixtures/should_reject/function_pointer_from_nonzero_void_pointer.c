/*
 * Only a zero integer constant expression cast to void * is a null pointer
 * constant. A nonzero void pointer cannot initialize a function pointer.
 * Expect ag_c E3099.
 */
typedef int callback_type(void);

int main(void) {
  callback_type *callback = (void *)1;
  return callback != 0;
}
