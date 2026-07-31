/* A VLA nested below a pointer still requires a complete element type. */
struct element;

int read_values(
    int count, struct element (*values)[count]);

int main(void) {
  return 0;
}
