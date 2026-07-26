static int read_const(int values[const 3]) {
  return values[0];
}

static int read_volatile(int values[volatile 3]) {
  return values[1];
}

static int read_restrict(int values[restrict 3]) {
  return values[2];
}

static int read_atomic(int values[_Atomic 3]) {
  int *original = values;
  values = original + 1;
  return values[-1] + values[1];
}

static int read_const_atomic(int values[const _Atomic 3]) {
  return values[1];
}

int main(void) {
  int values[3] = {4, 5, 6};
  if (read_const(values) != 4) return 1;
  if (read_volatile(values) != 5) return 2;
  if (read_restrict(values) != 6) return 3;
  if (read_atomic(values) != 10) return 4;
  if (read_const_atomic(values) != 5) return 5;
  return 0;
}
