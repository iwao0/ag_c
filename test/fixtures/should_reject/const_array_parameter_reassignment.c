// The const qualifier inside [] applies to the adjusted parameter pointer object.
int update(int values[const]) {
  values = 0;
  return 0;
}

int main(void) {
  return 0;
}
