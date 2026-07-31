/* An array nested below a pointer still requires a complete element type. */
struct incomplete;

static struct incomplete (*values)[2];

int main(void) {
  return values != 0;
}
