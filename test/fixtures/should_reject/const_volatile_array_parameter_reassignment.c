// Adding volatile does not make a const-qualified adjusted parameter modifiable.
int update(int values[const volatile 2]) {
  values = 0;
  return 0;
}

int main(void) {
  return 0;
}
