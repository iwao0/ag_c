/* _Noreturn cannot govern a declaration containing an object declarator. */
_Noreturn int function(void), object;

int main(void) {
  return 0;
}
