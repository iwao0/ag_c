/* A bit-field width must be an integer constant expression. */
int width;
struct record {
  unsigned int value : width;
};
int main(void) { return 0; }
