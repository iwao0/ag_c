/* inline cannot be attached to a function-pointer object. */
inline int (*function_pointer)(void);

int main(void) {
  return 0;
}
