/* Aggregate members cannot have a storage-class specifier. */
struct record {
  static int member;
};
int main(void) { return 0; }
