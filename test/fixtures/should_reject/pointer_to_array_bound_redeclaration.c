/* Function redeclarations must preserve inner pointer-to-array bounds. */
int sum_row(int (*row)[2]);

int sum_row(int (*row)[3]) {
  return (*row)[0];
}
