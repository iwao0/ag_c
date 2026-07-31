/* A pointer type name cannot hide an array with an incomplete element type. */
struct incomplete;

int main(void) {
  return sizeof(struct incomplete (*)[2]);
}
