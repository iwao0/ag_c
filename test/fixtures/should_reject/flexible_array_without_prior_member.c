/* A flexible array member requires another named struct member. */
struct record {
  int values[];
};
int main(void) { return 0; }
