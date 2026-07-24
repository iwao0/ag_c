static const int value = 7;

static int *invalid_return(void) {
  return &value;
}

int main(void) {
  return *invalid_return();
}
