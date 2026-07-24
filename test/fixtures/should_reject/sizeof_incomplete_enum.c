/* sizeof cannot query an incomplete enum, including inside its definition. */
enum value {
  VALUE_SIZE = sizeof(enum value)
};
int main(void) { return 0; }
