/* ARM64 aggregate edge cases exposed by c-testsuite 00204.
 * - `char x[1]` struct members must remain arrays for global string init and
 *   expression decay (`g.x` as a pointer).
 * - Global 3/9B structs used as value args/returns need the same indirect
 *   aggregate path as locals.
 * - Variadic aggregate args are stack bytes, not a single pointer slot; va_arg
 *   must advance by sizeof(type) rounded to 8. */
#include <assert.h>
#include <stdarg.h>

struct C1 { char x[1]; } gc1 = { "Z" };
struct S3 { char x[3]; } gs3 = { "abc" };
struct S7 { char x[7]; } gs7 = { "lmnopqr" };
struct S9 { char x[9]; } gs9 = { "ABCDEFGHI" };
struct D3 { double a, b, c; } gd3 = { 1.25, 2.5, 5.0 };
struct F4 { float a, b, c, d; } gf4 = { 3.5f, 4.5f, 5.5f, 6.5f };

int first_char(const char *p) { return p[0]; }
int sum_s3(struct S3 s) { return s.x[0] + s.x[1] + s.x[2]; }
struct S3 ret_s3(void) { return gs3; }

int check_varargs(int marker, ...) {
  va_list ap;
  va_start(ap, marker);
  struct S7 a = va_arg(ap, struct S7);
  struct S9 b = va_arg(ap, struct S9);
  struct D3 d = va_arg(ap, struct D3);
  struct F4 f = va_arg(ap, struct F4);
  va_end(ap);
  assert(a.x[0] == 'l' && a.x[6] == 'r');
  assert(b.x[0] == 'A' && b.x[8] == 'I');
  assert(d.a == 1.25 && d.b == 2.5 && d.c == 5.0);
  assert(f.a == 3.5f && f.b == 4.5f && f.c == 5.5f && f.d == 6.5f);
  return marker + a.x[1] + b.x[1];
}

int main(void) {
  assert(first_char(gc1.x) == 'Z');
  assert(sum_s3(gs3) == ('a' + 'b' + 'c'));
  struct S3 r = ret_s3();
  assert(r.x[0] == 'a' && r.x[2] == 'c');
  assert(check_varargs(10, gs7, gs9, gd3, gf4) == 10 + 'm' + 'B');
  return 0;
}
