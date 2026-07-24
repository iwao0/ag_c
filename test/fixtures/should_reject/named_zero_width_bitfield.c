/* A zero-width bit-field must be unnamed. */
struct record {
  unsigned int value : 0;
};
int main(void) { return 0; }
