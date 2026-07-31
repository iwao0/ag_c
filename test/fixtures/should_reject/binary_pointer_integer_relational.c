/* Relational operators cannot compare a pointer with an integer. */
int main(void) {
  int value = 1;
  return &value < 1;
}
