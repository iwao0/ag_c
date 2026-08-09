/* Each multidimensional character-array row enforces its own bound. */
int main(void) {
  char rows[1][2] = {"abc"};
  return rows[0][0];
}
