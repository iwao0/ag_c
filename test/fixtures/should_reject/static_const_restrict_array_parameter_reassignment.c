// A static minimum bound does not remove the adjusted pointer's const qualifier.
int update(int values[static const restrict 2]) {
  values = 0;
  return 0;
}

int main(void) {
  return 0;
}
