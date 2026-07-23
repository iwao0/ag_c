struct record;
union choice;

static struct record make_record(int value);
static union choice make_choice(int value);

struct record {
  int value;
};

union choice {
  int value;
  long other;
};

static struct record make_record(int value) {
  struct record result = {value};
  return result;
}

static union choice make_choice(int value) {
  union choice result = {.value = value};
  return result;
}

int main(void) {
  return make_record(20).value + make_choice(22).value != 42;
}
