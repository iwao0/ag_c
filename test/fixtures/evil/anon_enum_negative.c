// 無名 enum で負の開始値から連番
// A=-3, B=-2, C=-1, D=0
// 期待: exit=0
int main(void) {
    enum { A = -3, B, C, D };
    return D;
}
