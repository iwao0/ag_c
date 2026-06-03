// typedef 多次元配列に初期化子リストで値を入れる。
// `M2 m = {{1,2,3},{4,5,6}};` で M2 = int[2][3]。
// m[1][2] = 6。
// 期待: exit=6
typedef int M2[2][3];
int main(void) {
    M2 m = {{1, 2, 3}, {4, 5, 6}};
    return m[1][2];
}
