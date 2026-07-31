/* _Noreturn is invalid in a function type name. */
int main(void) {
  return sizeof(_Noreturn int (void));
}
