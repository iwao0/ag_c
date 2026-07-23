struct incomplete;

static struct incomplete (*values)[2];

int main(void) {
  return values != 0;
}
