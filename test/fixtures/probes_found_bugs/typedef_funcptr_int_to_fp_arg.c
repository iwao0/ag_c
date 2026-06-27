typedef double (*DOp)(double);
typedef float (*FOp)(float);

double add_half(double x) {
  return x + 0.5;
}

float add_quarter(float x) {
  return x + 0.25f;
}

DOp global_dop = add_half;
FOp global_fop = add_quarter;

int main(void) {
  DOp local_dop = add_half;
  FOp local_fop = add_quarter;
  double (*direct_dop)(double) = add_half;

  if (local_dop(3) != 3.5) return 1;
  if (global_dop(4) != 4.5) return 2;
  if (direct_dop(5) != 5.5) return 3;

  float lf = local_fop(6);
  float gf = global_fop(7);
  if (lf < 6.24f || lf > 6.26f) return 4;
  if (gf < 7.24f || gf > 7.26f) return 5;

  return 0;
}
