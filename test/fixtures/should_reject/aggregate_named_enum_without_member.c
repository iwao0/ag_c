/* A named enum definition without a member declarator declares no member. */
struct Item {
  enum State { STATE_READY = 1 };
  int value;
};
