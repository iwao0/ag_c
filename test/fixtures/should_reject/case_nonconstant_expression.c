static int runtime_value(void) {
  return 1;
}

int main(int argc, char **argv) {
  (void)argv;
  switch (argc) {
    case runtime_value():
      return 1;
  }
  return 0;
}
