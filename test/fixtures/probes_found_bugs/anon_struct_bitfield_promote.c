// 続き57: 匿名 struct/union 内の bitfield 昇格 (miscompile 修正)。
//
// C11 6.7.2.1p13: タグ名/メンバ名がない匿名 struct/union のメンバは外側 tag に
// 直接見えるよう昇格 (promote) される。struct_layout の昇格処理が bitfield 属性
// (bit_width / bit_offset / bit_is_signed) を明示的に 0 クリアしており、`s.lo` のような
// 昇格された bitfield アクセスが bitfield 抽出を経ず full-width load されて
// 値が化けていた。
//
// 修正: 旧 non-bf 版の名残のクリア処理を削除。`_mi = im;` のコピーで bitfield 情報を
// 保持する。
#include <assert.h>

/* (a) ハーフワード分割: 32bit を lo/hi の 16bit に分解 */
struct HiLo {
    int a;
    union {
        unsigned int n;
        struct { unsigned lo : 16; unsigned hi : 16; };
    };
};

/* (b) フラグセット: ビット単位の名前付きフィールド */
struct Flags {
    union {
        unsigned int raw;
        struct {
            unsigned f0 : 1;
            unsigned f1 : 1;
            unsigned f2 : 1;
            unsigned rest : 29;
        };
    };
};

/* (c) signed bitfield も昇格 — 符号拡張も維持される */
struct SignedHiLo {
    union {
        int raw;
        struct { int sl : 16; int sh : 16; };
    };
};

int main(void) {
    /* (a) HiLo: 0xdeadbeef を lo/hi に分解 */
    struct HiLo h;
    h.n = 0xdeadbeef;
    assert(h.lo == 0xbeef);
    assert(h.hi == 0xdead);

    /* lo/hi へ書いて n を読む */
    h.lo = 0x1234;
    h.hi = 0x5678;
    assert(h.n == 0x56781234);

    /* (b) Flags */
    struct Flags fl;
    fl.raw = 0;
    fl.f0 = 1;
    fl.f2 = 1;
    assert(fl.raw == 0x5);
    fl.rest = 100;
    assert(fl.f0 == 1);
    assert(fl.f1 == 0);
    assert(fl.f2 == 1);
    assert(fl.rest == 100);

    /* (c) signed bitfield 昇格 */
    struct SignedHiLo s;
    s.raw = (unsigned)((-1 & 0xffff) | ((-2 & 0xffff) << 16));
    assert(s.sl == -1);
    assert(s.sh == -2);

    return 0;
}
