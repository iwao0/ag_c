// static ローカルの float/double 変数の初期化子が 0 になっていた。
// try_lower_static_local_scalar が NUM の整数 ->val だけを読み、float リテラルの値
// (->fval) を無視して `.long 0` を出力していた。fp なら fval を gv->fval/fp_kind に
// 伝播して修正。
float running(float x){
  static float total = 0.0f;
  total += x;
  return total;
}
double dcount(void){
  static double c = 100.5;
  c += 1.0;
  return c;
}
int main(void){
  int t = 0;

  // static スカラ float の初期化子が値を保持する
  static float pi = 3.5f;
  t += (pi == 3.5f);
  pi += 1.0f;
  t += (pi == 4.5f);

  static double e = 2.5;
  t += (e == 2.5);

  // 関数内 static scalar (累積、初期化は 1 度だけ)
  running(10.0f);
  running(20.0f);
  t += ((int)running(0.0f) == 30);   // 累積 30

  t += ((int)(dcount() * 2) == 203); // 101.5 * 2 = 203
  t += ((int)(dcount() * 2) == 205); // 102.5 * 2 = 205 (1 度だけ init)

  return t + 36;  // 6 checks -> 6+36 = 42
}
