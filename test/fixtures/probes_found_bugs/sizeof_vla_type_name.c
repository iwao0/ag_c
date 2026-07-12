int main(void) {
  int n = 3;
  if (sizeof(int[n]) != 12) return 1;
  n = 5;
  if (sizeof(int[n]) != 20) return 2;
  int m = 2;
  if (sizeof(int[m][n]) != 40) return 3;
  if (sizeof(int (*[m])[n]) != 16) return 4;
  n = 3;
  if (sizeof(int[n++]) != 12) return 5;
  if (n != 4) return 6;
  return 0;
}
