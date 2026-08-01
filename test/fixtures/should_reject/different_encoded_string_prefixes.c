/* Adjacent string literals cannot combine incompatible UTF-16 and UTF-32 prefixes. */
int main(void) {
  return u"a" U"b"[0];
}
