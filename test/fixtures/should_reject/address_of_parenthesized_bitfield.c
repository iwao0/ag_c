struct Bits {
  unsigned int value : 3;
};

int main(void) {
  struct Bits bits = {1};
  unsigned int *pointer = &(bits.value);
  return pointer != 0;
}
