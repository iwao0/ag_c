/* A character-array member must hold every non-terminating string byte. */
struct Holder {
  char text[2];
};

int main(void) {
  struct Holder holder = {.text = "abc"};
  return holder.text[0];
}
