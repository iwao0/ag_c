/* sizeof cannot be applied to an expression with incomplete record type. */
struct incomplete;

int main(void) {
  return (int)sizeof(*(struct incomplete *)0);
}
