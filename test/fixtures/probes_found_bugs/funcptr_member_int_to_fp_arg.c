typedef double (*DOp)(double);
typedef float (*FOp)(float);

struct Ops {
  double (*direct)(double);
  DOp td;
  FOp ft;
  double (*arr[2])(double);
};

double add_half(double x) {
  return x + 0.5;
}

double add_one(double x) {
  return x + 1.0;
}

float add_quarter(float x) {
  return x + 0.25f;
}

int main(void) {
  struct Ops ops = { add_half, add_half, add_quarter, { add_half, add_one } };
  struct Ops *p = &ops;

  if (ops.direct(3) != 3.5) return 1;
  if (p->direct(4) != 4.5) return 2;
  if (ops.td(5) != 5.5) return 3;
  if (ops.arr[0](6) != 6.5) return 4;
  if (ops.arr[1](7) != 8.0) return 5;

  float f = ops.ft(8);
  if (f < 8.24f || f > 8.26f) return 6;

  return 0;
}
