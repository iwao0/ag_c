static int invalid_return(void) {
  return;
}

int main(void) {
  return invalid_return();
}
