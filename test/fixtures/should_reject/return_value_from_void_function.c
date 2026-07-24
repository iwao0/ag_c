static void invalid_return(void) {
  return 1;
}

int main(void) {
  invalid_return();
  return 0;
}
