// 関数名を () で囲んだ inline union タグ戻り値 (パース確認)
// 期待: exit=0
union U { int x; } (f)(void) {
    union U u;
    return u;
}
int main(void) { return 0; }
