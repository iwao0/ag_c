/* inline cannot govern a declaration containing an object declarator. */
inline int function(void), object;

int main(void) {
  return 0;
}
