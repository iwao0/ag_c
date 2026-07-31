// A case label requires an integer constant expression, not a floating constant.
int main(void) {
  switch (1) {
    case 1.0:
      return 0;
  }
  return 1;
}
