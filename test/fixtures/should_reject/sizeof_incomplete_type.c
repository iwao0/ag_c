/* sizeof cannot be applied to an incomplete record type. */
struct incomplete;

int main(void) {
  return (int)sizeof(struct incomplete);
}
