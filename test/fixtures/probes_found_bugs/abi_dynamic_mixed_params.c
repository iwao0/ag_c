/* Cross both removed fixed limits: 36 source parameters exceed the former
 * 16-entry callable signature and 32-entry ABI arrays. Exercise direct and
 * indirect calls with independent integer/floating-point register classes. */
#include <assert.h>

static long sum36(
    long a0, double a1, long a2, double a3, long a4, double a5,
    long a6, double a7, long a8, double a9, long a10, double a11,
    long a12, double a13, long a14, double a15, long a16, double a17,
    long a18, double a19, long a20, double a21, long a22, double a23,
    long a24, double a25, long a26, double a27, long a28, double a29,
    long a30, double a31, long a32, double a33, long a34, double a35) {
  return a0 + (long)a1 + a2 + (long)a3 + a4 + (long)a5 +
         a6 + (long)a7 + a8 + (long)a9 + a10 + (long)a11 +
         a12 + (long)a13 + a14 + (long)a15 + a16 + (long)a17 +
         a18 + (long)a19 + a20 + (long)a21 + a22 + (long)a23 +
         a24 + (long)a25 + a26 + (long)a27 + a28 + (long)a29 +
         a30 + (long)a31 + a32 + (long)a33 + a34 + (long)a35;
}

typedef long (*sum36_fn)(
    long, double, long, double, long, double,
    long, double, long, double, long, double,
    long, double, long, double, long, double,
    long, double, long, double, long, double,
    long, double, long, double, long, double,
    long, double, long, double, long, double);

int main(void) {
  sum36_fn indirect = sum36;

  assert(sum36(
             1, 2.0, 3, 4.0, 5, 6.0, 7, 8.0, 9, 10.0,
             11, 12.0, 13, 14.0, 15, 16.0, 17, 18.0, 19, 20.0,
             21, 22.0, 23, 24.0, 25, 26.0, 27, 28.0, 29, 30.0,
             31, 32.0, 33, 34.0, 35, 36.0) == 666);
  assert(indirect(
             36, 35.0, 34, 33.0, 32, 31.0, 30, 29.0, 28, 27.0,
             26, 25.0, 24, 23.0, 22, 21.0, 20, 19.0, 18, 17.0,
             16, 15.0, 14, 13.0, 12, 11.0, 10, 9.0, 8, 7.0,
             6, 5.0, 4, 3.0, 2, 1.0) == 666);
  return 0;
}
