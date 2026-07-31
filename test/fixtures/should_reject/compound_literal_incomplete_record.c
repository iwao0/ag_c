/* A compound literal requires a complete object type. */
struct incomplete;

int main(void) {
  (void)(struct incomplete){0};
  return 0;
}
