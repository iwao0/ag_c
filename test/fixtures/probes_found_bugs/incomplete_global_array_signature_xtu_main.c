extern int incomplete_global_values[];

int main(void) {
  return incomplete_global_values[0] == 40 &&
                 incomplete_global_values[2] == 42
             ? 0
             : 1;
}
