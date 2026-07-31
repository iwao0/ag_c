int sum_cube(int (*cube)[][3]);

int sum_cube(int (*cube)[][4]) {
  return (*cube)[0][0];
}

int main(void) {
  int values[1][3] = {{42, 0, 0}};
  return sum_cube(&values) != 42;
}
