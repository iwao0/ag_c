typedef int (*Iop)(int);
typedef long (*Lop)(long);
typedef int (*Mix)(int, long, int);

struct Ops {
  int (*direct)(int);
  Iop td;
  Lop lg;
  Mix mix;
  int (*arr[2])(int);
};

int plus3(int x) { return x + 3; }
long plus5(long x) { return x + 5; }
int mix3(int a, long b, int c) { return a + (int)b + c; }
double ret_double(int x) { return x + 0.5; }
float ret_float(long x) { return (float)x + 0.25f; }

Iop gop = plus3;
Mix gmix = mix3;
int (*garr[2])(int) = { plus3, plus3 };
double (*gdarr[2])(int) = { ret_double, ret_double };
float (*gfarr[2])(long) = { ret_float, ret_float };

int main(void) {
  int (*fp)(int) = plus3;
  Iop op = plus3;
  Mix lm = mix3;
  struct Ops ops = { plus3, plus3, plus5, mix3, { plus3, plus3 } };
  struct Ops *p = &ops;
  int (*larr[2])(int) = { plus3, plus3 };
  double (*ldarr[2])(int) = { ret_double, ret_double };
  float (*lfarr[2])(long) = { ret_float, ret_float };

  if (fp(7.9) != 10) return 1;
  if ((*fp)(8.9f) != 11) return 2;
  if (op(9.9) != 12) return 3;
  if (gop(10.9f) != 13) return 4;
  if (ops.direct(11.9) != 14) return 5;
  if (p->direct(12.9f) != 15) return 6;
  if (ops.td(13.9) != 16) return 7;
  if (ops.arr[0](14.9f) != 17) return 8;
  if (ops.lg(20.9) != 25) return 9;
  if (gmix(1.9, 20.9, 3.9f) != 24) return 10;
  if (lm(2.9f, 30.9, 4.9) != 36) return 11;
  if (ops.mix(5.9, 60.9, 6.9f) != 71) return 12;
  if (larr[0](40.9) != 43) return 13;
  if (garr[1](50.9f) != 53) return 14;
  if (ldarr[0](6.9) != 6.5) return 15;
  if (gdarr[1](7.9f) != 7.5) return 16;

  float a = lfarr[0](8.9);
  if (a < 8.24f || a > 8.26f) return 17;
  float b = gfarr[1](9.9f);
  if (b < 9.24f || b > 9.26f) return 18;

  return 0;
}
