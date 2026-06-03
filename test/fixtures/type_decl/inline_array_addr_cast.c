// inline 多次元配列の `&arr` を (int*) キャストして使う。
// 修正前: 配列名 `arr` は build_array_lvar_addr_node により ND_ADDR(ND_LVAR)
// として表現されていたため、`&arr` でさらに ND_ADDR でラップすると codegen
// (gen_lval) が ND_ADDR(ND_ADDR(...)) を受理できず E4002 になっていた。
// 修正: build_unary_addr_node で operand が既に ND_ADDR ならそのまま返す
// (C 仕様: 配列名 `arr` と `&arr` はアドレス値が同じ)。
// 期待: exit=99 ((int*)&arr の先頭要素 = arr[0][0][0])
int main(void) {
    int arr[2][3][4];
    arr[0][0][0] = 99;
    int *p = (int*)&arr;
    return p[0];
}
