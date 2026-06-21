/* ポインタ typedef を基底とする仮引数 (`typedef char* Str; int len(Str s){ s[i] }`) が
 * subscript で E3064 (両辺ともポインタ/配列でない) になっていた。原因: param_decl_spec_t が
 * typedef のポインタ性 (_ti.is_pointer) を捕捉せず、宣言子側に `*` が無い (param_is_ptr=0)
 * ため register_param_lvar のポインタ分岐に入らずスカラ登録されていた。deref `*s` は
 * build_unary_deref が 8B 値を寛容に許すため動いていたが、subscript は ps_node_is_pointer が
 * 偽を返し弾かれていた。直書き `const char* s` 仮引数は元から動作。
 * 修正: typedef 基底のポインタ段数を param_decl_spec_t に捕捉し、宣言子の `*` と合成して
 * 実効ポインタ性 (param_is_ptr / param_ptr_levels) を決める。 */
#include <assert.h>

typedef char*          Str;
typedef const char*    CStr;
typedef int*           IP;
typedef IP*            IPP;     /* 多段 */
struct S { int x, y; };
typedef struct S*      SP;

int slen(CStr s)        { int n = 0; while (s[n]) n++; return n; }
int isum(IP p)          { return p[0] + p[1] + p[2]; }
int second(IP p)        { IP q = p + 1; return *q; }       /* ポインタ算術も */
char setc(Str s)        { s[0] = 'X'; return s[0]; }        /* 添字代入 */
int deref2(IPP pp)      { return **pp; }                    /* 多段 typedef */
int via_star(IP* pp)    { return (*pp)[1]; }                /* typedef + 宣言子 `*` */
int smemb(SP s)         { return s->x + s->y; }             /* struct ポインタ typedef */

int main(void) {
  assert(slen("hello world") == 11);
  int a[3] = { 10, 20, 30 };
  assert(isum(a) == 60);
  assert(second(a) == 20);
  char buf[4] = "abc";
  assert(setc(buf) == 'X' && buf[0] == 'X');
  int x = 42; int *p = &x;
  assert(deref2(&p) == 42);
  int pair[2] = { 5, 9 };
  int *pp = pair;
  assert(via_star(&pp) == 9);
  struct S s = { 6, 7 };
  assert(smemb(&s) == 13);
  /* ローカル変数経由 (回帰防止) */
  IP lp = a;
  assert(lp[0] + lp[2] == 40);
  return 0;
}
