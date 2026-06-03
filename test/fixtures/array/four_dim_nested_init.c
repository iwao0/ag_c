// 4 次元配列のネスト初期化と添字アクセス
// 修正前: 3D まで (outer_stride / mid_stride / next_deref_size の 3 段) しか
//        サポートしておらず、4D は a[i][j][k][l] が SEGV、
//        ネスト初期化 `{{{{...}}}}` も拒否されていた。
// 対応: lvar_t / node_mem_t に extra_strides[5] + count を追加し、
//      サブスクリプト毎に next_deref_size → extra_strides[0] → ... と
//      シフトさせる。最大 8 次元まで対応。
//      初期化子は parse_array_init_chunk を再帰させて任意のネスト深度を
//      受理する。
// 期待: exit=24
int main(void) {
    int a[2][2][2][3] = {
      {{{1,2,3},{4,5,6}}, {{7,8,9},{10,11,12}}},
      {{{13,14,15},{16,17,18}}, {{19,20,21},{22,23,24}}}
    };
    return a[1][1][1][2];
}
