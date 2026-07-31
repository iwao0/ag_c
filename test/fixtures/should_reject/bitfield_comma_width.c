/* A comma expression is not an integer constant expression for bit-field width. */
struct record {
  unsigned int value : (1, 3);
};
int main(void) { return 0; }
