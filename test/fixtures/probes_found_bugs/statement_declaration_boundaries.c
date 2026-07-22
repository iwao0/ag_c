int main(void) {
  if (1) {
    int value = 1;
    if (!value) return 1;
  } else {
    int value = 0;
    return value;
  }

  while (0) {
    int value = 0;
    return value;
  }

  do {
    int value = 0;
    if (value) return 2;
  } while (0);

  for (int once = 0; once < 1; once++) {
    int value = once;
    if (value) return 3;
  }

  switch (1) {
    int value;
    case 1:
      value = 0;
      return value;
  }
}
