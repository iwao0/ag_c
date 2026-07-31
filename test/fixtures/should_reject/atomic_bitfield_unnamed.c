/* An unnamed zero-width bit-field also cannot have atomic type. */
struct flags {
  _Atomic unsigned int : 0;
};

int main(void) {
  return 0;
}
