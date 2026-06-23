// c-testsuite 00213: GNU statement expression ({ ...; expr })。
// 修正前: 三項演算子の偽側 `({ ... })` で E3064。
extern int printf(const char *, ...);
static void kb_wait(void) {
  unsigned long timeout = 2;
  do {
    (1 ? printf("timeout=%ld\n", timeout) :
        ({
          int i = 1;
          while (1)
            while (i--)
              some_label:
                printf("error\n");
          goto some_label;
        }));
    timeout--;
  } while (timeout);
}
int main(void) {
  int i = ({
    int j = 1;
    if (0) {
      while (j--) {
        printf("SEonce\n");
      enterexprloop:
        printf("SEtwice\n");
      }
    }
    if (j >= 0)
      goto enterexprloop;
    j;
  });
  (void)i;
  kb_wait();
  return 0;
}
