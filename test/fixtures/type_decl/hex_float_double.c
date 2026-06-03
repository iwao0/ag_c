// 16 進浮動小数点リテラル 0x1.8p+3 = 1.5 * 8 = 12.0
// 期待: 12.0
double ag_m(void) { double d = 0x1.8p+3; return d; }
