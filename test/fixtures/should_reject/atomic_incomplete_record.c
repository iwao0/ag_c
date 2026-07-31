/* An atomic type specifier requires a complete object type. */
struct incomplete;

_Atomic(struct incomplete) value;

int main(void) {
  return 0;
}
