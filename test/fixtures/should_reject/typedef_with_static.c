/* A typedef declaration cannot combine typedef with static. */
typedef static int value_type;

int main(void) {
  return 0;
}
