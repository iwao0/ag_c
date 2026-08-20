/* A static assertion does not declare a named member. */
struct Check {
  _Static_assert(1, "ok");
};

int main(void) { return 0; }
