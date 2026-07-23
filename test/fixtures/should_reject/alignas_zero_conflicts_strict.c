_Alignas(0) extern int value;
_Alignas(16) int value;

int main(void) {
  return value;
}
