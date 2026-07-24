/* A floating type is not an implementation-defined integer bit-field type. */
struct record {
  double value : 1;
};
int main(void) { return 0; }
