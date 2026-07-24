/* A flexible array member is permitted only as the last struct member. */
union record {
  int scalar;
  int values[];
};
int main(void) { return 0; }
