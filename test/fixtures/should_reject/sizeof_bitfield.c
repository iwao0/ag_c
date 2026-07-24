/* sizeof cannot be applied to a bit-field designator. */
struct record {
  unsigned int value : 3;
};
int main(void) {
  struct record object = {0};
  return sizeof object.value;
}
