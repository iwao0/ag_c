/* _Noreturn cannot be attached to a function-pointer object. */
_Noreturn int (*function_pointer)(void);

int main(void) {
  return 0;
}
