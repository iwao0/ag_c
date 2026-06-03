// typedef 多次元配列の `&arr` を (int*) キャストして使う。
// 配列名 `arr` (typedef int M[2][3][4]; M arr;) は build_array_lvar_addr_node
// により ND_ADDR(ND_LVAR) として表現される。`&arr` でラップすると codegen で
// ND_ADDR(ND_ADDR(...)) になり E4002 で失敗していたのを修正。
// 期待: exit=99 ((int*)&arr の先頭要素 = arr[0][0][0])
typedef int M[2][3][4];
int main(void) {
    M arr;
    arr[0][0][0] = 99;
    int *p = (int*)&arr;
    return p[0];
}
