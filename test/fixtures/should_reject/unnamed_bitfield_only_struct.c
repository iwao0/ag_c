/* An unnamed bit-field does not supply a named member. */
struct Bits {
  unsigned int : 1;
};

int main(void) { return 0; }
