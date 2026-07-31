/* inline cannot be attached to a typedef for a function type. */
typedef inline int function_type(void);

int main(void) {
  return 0;
}
