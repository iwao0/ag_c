/* No member may follow a flexible array member. */
struct record {
  int count;
  int values[];
  int tail;
};
int main(void) { return 0; }
