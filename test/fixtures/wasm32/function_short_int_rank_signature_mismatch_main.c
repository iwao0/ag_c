// short and int remain different function types even when a target gives
// both parameters and results the same low-level Wasm i32 representation.
short transform_integer_rank(short value);

int main(void) {
  return transform_integer_rank((short)7) == 12 ? 0 : 1;
}
