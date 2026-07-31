/* A comma expression in an evaluated logical operand is not an ICE. */
_Static_assert(1 && (2, 3),
               "evaluated comma is not allowed in an integer constant expression");

int main(void) {
  return 0;
}
