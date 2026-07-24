struct incomplete;

int main(void) {
  (void)(struct incomplete){0};
  return 0;
}
