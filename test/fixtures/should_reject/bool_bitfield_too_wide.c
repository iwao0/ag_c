/* A _Bool bit-field cannot be wider than one bit. */
struct record {
  _Bool value : 2;
};
int main(void) { return 0; }
