/* An aggregate member cannot use the _Thread_local storage class. */
struct value {
  _Thread_local int member;
};

int main(void) {
  return 0;
}
