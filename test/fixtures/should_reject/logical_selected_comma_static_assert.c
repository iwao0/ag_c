_Static_assert(1 && (2, 3),
               "evaluated comma is not allowed in an integer constant expression");

int main(void) {
  return 0;
}
