/* The left operand of -> must have pointer type. */
struct value {
  int member;
};

int main(void) {
  struct value value = {1};
  return value->member;
}
