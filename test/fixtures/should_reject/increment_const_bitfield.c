struct Bits {
  const unsigned int value : 3;
};

int main(void) {
  struct Bits bits = {1};
  bits.value++;
  return 0;
}
