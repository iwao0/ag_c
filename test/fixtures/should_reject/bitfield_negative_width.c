/* A bit-field width cannot be negative. */
struct record {
  unsigned int value : -1;
};
int main(void) { return 0; }
