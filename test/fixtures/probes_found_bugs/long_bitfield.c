// long / long long のビットフィールド (>32bit、または 32bit ユニットを跨ぐ) が
// 壊れていたバグ。
// (1) struct_layout がビットフィールドのストレージユニットを常に 4 バイトに
//     クランプしていた。`unsigned long a:40` が 32bit ユニットに収まらず、
//     後続フィールドの bit_offset 計算が破綻して a と b を同じ offset 0 に重複配置
//     し、一方の書き込みが他方を破壊していた。
// (2) codegen (emit_bitfield_load/store) がビットフィールドの load/shift/mask を
//     常に 32bit で行い、bit_offset+bit_width>32 のフィールドで上位ビットを失って
//     いた。さらに 64bit マスク即値 (0xFFFFFF0000000000 等) を AND の即値に直接
//     渡すと下位 32bit しか materialize されず隣接フィールドを破壊した。
// 修正: storage_size を 8 まで許可 (long bitfield は 8 バイトユニット)。codegen は
//   offset+width>32 で 64bit ロード/シフト/マスクを使い、マスク定数はレジスタへ
//   展開してから AND/OR する。
// 修正前: 値破壊 / フィールド重複
// 期待: exit=42
struct Packed {
    unsigned long a : 40;   // bits 0-39
    unsigned long b : 20;   // bits 40-59  (同一 8 バイトユニット内)
};

struct Mixed {
    unsigned int  x : 10;
    unsigned long y : 40;
};

int main(void) {
    struct Packed p;
    p.a = 1000000000000UL;          // 10^12 (40bit に収まる)
    p.b = 500000;
    // b の書き込みが a を壊さない / a の書き込みが b を壊さない
    if (p.a != 1000000000000UL) return 1;
    if (p.b != 500000) return 2;

    // 書き込み順を変えても独立
    struct Packed q;
    q.b = 999999;
    q.a = 1000000000000UL;
    if (q.b != 999999 || q.a != 1000000000000UL) return 3;

    // int と long のビットフィールド混在 (y:40 の最大は 2^40-1 ≈ 1.1e12)
    struct Mixed m;
    m.x = 1000;
    m.y = 1000000000000UL;
    if (m.x != 1000 || m.y != 1000000000000UL) return 4;

    // 配列内でも独立
    struct Packed arr[2];
    arr[0].a = 111111111111UL; arr[0].b = 1;
    arr[1].a = 222222222222UL; arr[1].b = 2;
    if (arr[0].a != 111111111111UL || arr[1].b != 2) return 5;

    return (int)(p.a / 1000000000000UL) * 42;   // 1 * 42 = 42
}
