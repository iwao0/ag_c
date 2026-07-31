/* Compound pointer addition requires a complete pointed-to array type. */
typedef int incomplete_array[];

int main(void) {
  incomplete_array *pointer = 0;
  pointer += 1;
  return pointer != 0;
}
