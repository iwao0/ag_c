// c-testsuite 00129: typedef 名と同名ラベル `s:` の shadowing。
// 修正前: `s:` が typedef 宣言として解釈され E3064。
typedef struct s s;
struct s {
  struct s1 {
    int s;
    struct s2 {
      int s;
    } s1;
  } s;
} s2;
#define s s
int main(void) {
#undef s
  goto s;
  struct s s;
  {
    int s;
    return s;
  }
  return s.s.s + s.s.s1.s;
s:
  {
    return 0;
  }
  return 1;
}
