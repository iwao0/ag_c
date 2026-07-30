// Two specified constant array bounds remain incompatible even though both
// pointer parameters lower to the same Wasm i32 value type.

int mismatched_array_bound(int (*row)[2]);

int main(void) {
  int row[2] = {20, 22};
  return mismatched_array_bound(&row);
}
