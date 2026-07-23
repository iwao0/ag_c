struct item;
_Atomic struct item *pointer;

int main(void) {
  return pointer != 0;
}
