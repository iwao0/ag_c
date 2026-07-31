/* An internal-linkage tentative definition requires a complete object type. */
struct value;
static struct value object;
struct value {
  int member;
};

int main(void) {
  return 0;
}
