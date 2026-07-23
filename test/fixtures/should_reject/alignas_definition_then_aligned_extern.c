int value = 1;
_Alignas(16) extern int value;

int main(void) {
  return value;
}
