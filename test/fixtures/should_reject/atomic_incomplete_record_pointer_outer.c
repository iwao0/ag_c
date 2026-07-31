/* A pointer declarator cannot make an incomplete atomic record base type valid. */
struct item;
_Atomic struct item *pointer;

int main(void) {
  return pointer != 0;
}
