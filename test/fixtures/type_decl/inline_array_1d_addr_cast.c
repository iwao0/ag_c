// 1 次元配列に対する `&arr` のキャスト経由アクセス。
// 配列名は ND_ADDR(ND_LVAR) として表現されているため `&arr` でラップしても
// codegen で E4002 にならないよう build_unary_addr_node が ND_ADDR をそのまま
// 返すよう修正済み。配列要素のアドレスは arr と &arr で一致する (C 仕様)。
// arr[2] = 33 を書いて (int*)&arr で [2] を読み戻す。
// 期待: exit=33
int main(void) {
    int arr[5];
    arr[2] = 33;
    int *p = (int*)&arr;
    return p[2];
}
