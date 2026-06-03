// 3 段ネストしたシャドウイング
// 外側 x=1 が最終的に有効
// 期待: exit=1
int main(void) {
    int x = 1;
    { int x = 2;
        { int x = 3; }
    }
    return x;
}
