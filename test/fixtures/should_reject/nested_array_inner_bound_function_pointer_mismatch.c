static int sum_cube(int (*cube)[][3]) {
  return (*cube)[0][0];
}

int main(void) {
  int (*invalid)(int (*)[][4]) = sum_cube;
  return invalid == 0;
}
