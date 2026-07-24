static void do_nothing(void) {
}

int main(void) {
  (void)(1 ? do_nothing() : 3);
  return 0;
}
