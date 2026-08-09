/* Braces do not allow string content to exceed an automatic array bound. */
int main(void) {
  char text[2] = {"abc"};
  return text[0];
}
