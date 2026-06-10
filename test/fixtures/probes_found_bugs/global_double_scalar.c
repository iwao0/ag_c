// グローバル double スカラの初期化
// 修正前: parser が node_num_t::fval を捨て、init_val (=0) で .quad 0 を出していた。
// さらに ND_GVAR ノードの fp_kind が伝播せず、読み出し側で整数 load されていた。
double v = 3.5;
int main(void) {
  return (int)(v * 4); // 14
}
// 期待: 14
