static int object;

static unsigned short narrow_address =
    (unsigned short)&object;

int main(void) {
  return narrow_address == 0;
}
