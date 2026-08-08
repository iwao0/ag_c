typedef char loop_name;

int main(void) {
  int iterations = 0;

  for (loop_name *loop_name = 0;
       sizeof(loop_name) == sizeof(char *) && iterations == 0;
       ++iterations) {
    if (sizeof(loop_name) != sizeof(char *)) return 1;
  }

  loop_name after_loop = 7;
  if (sizeof(after_loop) != sizeof(char)) return 2;
  return iterations != 1;
}
