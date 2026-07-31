// A void, zero-parameter unprototyped placeholder is ABI-shaped like an
// uninitialized function slot, but its definition must still clear wildcard
// metadata after direct and indirect calls are resolved.
// Expected: exit=0.

static void record_call();

static int call_count;

int main(void) {
  void (*taken_address)() = record_call;
  record_call();
  if (taken_address == 0 || call_count != 1)
    return 1;
  taken_address();
  return call_count == 2 ? 0 : 2;
}

static void record_call(void) {
  call_count++;
}
