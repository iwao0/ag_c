// An aggregate member cannot use a restrict-qualified pointer to a function
// because the pointed-to type is not an object or incomplete type.
struct callbacks {
  int (*restrict read)(void);
};

int main(void) {
  return 0;
}
