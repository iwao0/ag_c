# 差分テスト バグカバレッジ表

clang との差分テスト（同一 C ソースを ag_c と clang でコンパイルして exit code を比較）で
炙り出した miscompile / コンパイルエラーの **チェック済み領域** を管理する。同じ領域を
何度も探さないための索引。

最終更新: 2026-06-19（可変長無名固定引数 まで）

## 凡例（状態）
- ✅ **済**: チェック済みで現状 green（差分なし）。
- 🔧 **修正済**: バグを発見し修正＋回帰 fixture 登録済み（`test/fixtures/probes_found_bugs/`）。
- ⚠️ **未対応**: バグを確認済みだが未修正（既知の制約）。
- ⬜ **未チェック**: まだ系統的に差分テストしていない。

## 使い方
1. 新しい領域を差分テストする前にこの表を見て、未チェック（⬜）や未対応（⚠️）を優先する。
2. バグを修正したら行を 🔧 に更新し、fixture 名 / コミットを記入する。
3. 「型 × 宣言経路 × 使用文脈」で取りこぼしが多い（例: unsigned を直しても int だけ／local だけ／
   scalar だけ、になりがち）。1 つ直したら long/char/short・global・typedef・配列要素・
   ポインタ deref・struct メンバまで広げて確認し、各セルを更新する。
4. 手法・再現手順は `HANDOFF.md` の「作業のやり方」を参照。

---

## カバレッジ表

### 整数型の幅 / 符号 / キャスト
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| sub-int (char/short) の符号拡張・ゼロ拡張 | 🔧 | cast_short_char_sign_extend ほか | インライン使用で sign/zero-extend |
| (int)/(unsigned)/(signed) キャストの符号更新 | 🔧 | 5b474f7, 1317698 | 全経路網羅済み |
| (int)/(long) の 32/64bit 切り詰め・拡張 | 🔧 | 1c8e358, 8ddafa6 | (long)unsigned の zext 含む |
| long/long long リテラルの i64 型付け | 🔧 | 365d8c0 | |
| i32 比較の 32bit 幅 / sub-int 戻り値切り詰め | 🔧 | int_cmp_width_and_subint_return | |
| unsigned char/short 戻り値のゼロ拡張 | 🔧 | 1b5e1df | |
| typedef 経由 unsigned char/short (`uint8_t`) のロード符号性 | 🔧 | stdheader/stdint_uint8 | adjust_local_decl_spec_from_typedef が typedef の unsigned を上書きして捨て、1byte ロードが ldrsb に。200→-56 (exit truncation で偽装通過) |
| typedef 経由 unsigned char/short の**戻り値**ゼロ拡張 | 🔧 | typedef_unsigned_subint_return | resolve_func_ret_typedef が typedef の unsigned を捨て、sub-int 戻り値が符号拡張 (`u8 f(){return 200;}`→-56)。上記ローカルと同根の戻り型版 |
| `unsigned long`/`unsigned char` 戻りの符号性追跡 | ⚠️ | — | plain `unsigned` のみ。ret_token_kind が TK_LONG/CHAR に潰れる |
| 混在幅比較（片側 i32・片側 i64） | ⚠️ | — | gen_inst_int_cmp は両 i32 のみ 32bit |
| 複合代入の sub-int / unsigned wrap | ✅ | (probe p3) | |
| シフト境界・unsigned/signed 右シフト | ✅ | (probe p7) | |
| 符号付き除算・剰余（負値, long） | ✅ | (probe p5) | |

### 浮動小数（float / double）
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| 配列メンバ access/init | 🔧 | float_array_member, 10b9748 | |
| 多次元 subscript の fp load | 🔧 | multidim_float_array_subscript, 10d291d | |
| ブール条件分岐 / `&&` `||` | 🔧 | ec96e30, 87a24a1 | |
| static ローカル float init | 🔧 | f873777 | |
| fp ポインタ仮引数 `double *a` の deref/subscript | 🔧 | fp_pointer_parameter | |
| 匿名 struct/union メンバの fp 昇格 | 🔧 | anon_member_fp_unsigned_promote | |
| 可変長引数の double | ✅ | (probe q2) | |
| double 比較・三項 | ✅ | (probe q5) | |
| NaN / Inf の比較（`nan!=nan`, `1/inf`, denormal） | ✅ | (probe fp1, fp2) | |
| 単項マイナスの -0.0（`-0.0`, `-z`, `1.0/-0.0`=-inf） | 🔧 | fp_unary_minus_neg_zero | ND_FNEG/IR_FNEG。従来は `0.0-x` で符号消失 |

### 関数ポインタの FP 戻り値（`d0` で読む）
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| funcptr 変数 / 仮引数 / typedef / `(*p)()` | 🔧 | funcptr_fp_return, 0b980b0 | |
| 関数ポインタ配列の要素 `ops[i]()` | 🔧 | funcptr_array_fp_return, 45bd478 | |
| struct メンバ funcptr `s.f()` / `sp->f()` | 🔧 | funcptr_member_fp_return, 20c4b17 | |
| グローバル funcptr `gops()` | 🔧 | funcptr_global_fp_return, ada7696 | |
| グローバル funcptr **配列** `gops[i]()` の fp 戻り (N>=2) | 🔧 | funcptr_global_array_fp_return, e62862e | |
| 要素数 1 の括弧内配列グローバル `(*g[1])()` / `(*g[1])` | 🔧 | global_size1_funcptr_array | paren 内 `[1]` の有無で配列判定。funcptr/ポインタ両方 |
| 間接呼び出しの int→fp 引数昇格 `p(4)` | ⚠️ | — | 仮引数型保存が必要 |
| 可変長プロトタイプの無名固定引数 `int printf(const char*,...)` | 🔧 | variadic_unnamed_proto_fixed_args | 固定引数数 0 誤算→crash。定義なし外部関数で顕在化 |
| 間接呼び出しの int→fp 引数昇格 `fp(3)` | ⚠️ | — | 関数ポインタ経由 `double(*)(double)` 等に int/float 実引数を渡すと昇格されず fp レジスタにゴミ。直接呼び出しは正常 |
| **`void *` 戻り型を void 関数と誤判定** | 🔧 | void_ptr_return | stmt.c の return チェックが ret_token_kind==TK_VOID のみ見て is_pointer 無視→`void *f(){return p;}` が E3005 で弾かれ malloc 風関数が書けず |

### ポインタ / 配列 / subscript
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| 多段ポインタ subscript / pql 減算 | 🔧 | d0ff96c, 8e937e0 | |
| struct/union ポインタ算術スケール | 🔧 | b6a42ec | |
| struct ポインタ配列メンバ `h.arr[i]->v` | 🔧 | struct_ptr_array_member_access, db98d34 | |
| 多次元配列の行 decay `m[0]`, `**m`, `*(*(m+1)+2)` | 🔧 | a2a8328, 47975d4 | 3D まで |
| 局所「2D 配列へのポインタ」mid_stride | 🔧 | 5a47279 | |
| 配列へのポインタ（要素 >4B struct） | 🔧 | 10072cc | |
| インラインのポインタキャスト deref `*(int*)(cp+4)` | 🔧 | d44947f | |
| **グローバル data pointer の fp deref** `*dp`/`dp[i]` | 🔧 | global_fp_data_pointer_deref, 426ff01 | |
| **グローバル pointer-to-array** `(*pa)[N]` subscript | 🔧 | global_ptr_to_array_subscript, 9547728 | int/double |
| `(*dp)[j]` 単項 deref + subscript の fp load | 🔧 | ptr_to_array_deref_fp, 8cf0749 | local/global |
| **グローバル多次元 pointee** `(*pa)[N][M]` | 🔧 | global_ptr_to_multidim_array, eb74293 | int/double, 4D まで |
| 変則形 `*(t+1)[0]`（pointer-to-2D-array に subscript+deref 混在） | ⚠️ | — | SIGSEGV、別経路 |
| 大きいスタックフレーム（>4095B）の `sub sp`/`add x,x29,#off` | 🔧 | large_stack_frame | imm12 上限超を 4096 倍数部(lsl#12)+端数に分割。ldr/str は ~32KB まで別途 |
| ポインタ減算・比較 | ✅ | (probe p6) | |

### 集約初期化（C11 6.7.9）
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| 多次元配列メンバ / ネスト designator | 🔧 | 4b92768, aadf3b7 | |
| 重複 designator 後勝ち / 位置継続 | 🔧 | 4a5942d, df23e17 | C11 準拠 |
| struct 配列メンバ brace init | 🔧 | 1684a8c | |
| グローバル designator `.member[idx]`/`.member.sub` | 🔧 | global_designator_member_index, 1e843b4 | |
| ローカル designator の struct leaf brace init | 🔧 | local_designator_aggregate_leaf, 7e39081 | |
| `_Bool` 初期化子の 0/1 正規化（全経路） | 🔧 | 5b3d592 | |
| グローバルポインタ配列 `&data[n]`/`data+n` | 🔧 | global_ptr_array_addr_init, 138cd70 | |
| union 配列要素の brace init `arr[2]={[1]={.n=5}}` | ⚠️ | — | local union leaf は E3064 のまま除外 |
| グローバルのネスト brace 配列添字 `{.items={[2]={.a=7}}}` | ⚠️ | — | flat パーサ制約。`.items[2].a=` 形は対応済み |

### struct / union ABI・値渡し
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| 小さい struct（3/5/6/7B）の値渡し/返し | 🔧 | 050e1bf | |
| <=8B struct を ternary / funccall から初期化 | 🔧 | 96b5510, 7697cf8 | |
| static struct/union 局所の永続化 | 🔧 | static_local_struct_persist, 8167e8e | インライン定義の匿名タグは未対応 |
| long bitfield（>32bit / ユニット跨ぎ） | 🔧 | 040da11 | |
| bitfield の符号（signed sign-extend / unsigned）・幅切り詰め | ✅ | (probe u2, u6) | int:4/:1/:12/:20 等 |
| 非 bitfield メンバ直後の bitfield パッキング（`char c; int x:20`） | 🔧 | bitfield_pack_after_member | AAPCS: 収まれば同一 storage ユニットへ |
| pragma pack(1) と通常の混在 sizeof | ✅ | (probe bf3) | |
| ゼロ幅 bitfield `:0` のユニット分離 | ✅ | (probe bf1) | |

### 型機能 / その他
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| `_Alignof` の集約アラインメント | 🔧 | 3e8a4d1 | |
| `_Generic` の文字列/long リテラル/ポインタ種別 | 🔧 | e0b5190 ほか | |
| `_Generic` のスカラ整数キャスト制御式 `(char)x` | 🔧 | generic_scalar_cast_control | `(T)0` idiom。ポインタ/関数/タグは infer 維持 |
| `_Generic` の修飾子除去 / ポインタ段数 | ✅ | (probe gen1) | |
| `_Generic` 深いネスト型 (`(int(*(*)(void))[3])`) の同一型マッチ | ⚠️ | — | clang はマッチ、ag_c は default。複雑型の assoc 照合が不完全 |
| enum 値・算術 | ✅ | (probe q3) | |
| switch / fallthrough / default | ✅ | (probe p4) | |
| 複合代入演算子 `<<=` `>>=` `&=` `|=` `^=` `%=`（型幅・符号） | ✅ | (probe ca1, ca2) | sub-int/メンバ/ポインタ deref/配列要素 |
| do-while / goto / 多重代入 / 前後置インクリメント | ✅ | (probe q4, q7) | |
| compound literal（引数・式中） | ✅ | (probe q6) | |
| ファイルスコープ複合リテラル初期化子 `int x=(int){42};` | ⚠️ | — | **先行する関数宣言**があると初期化子が失われ x=0（clang=42）。グローバル変数の先行では起きない。assert.h 取込で顕在化 |
| 可変長引数（int） | ✅ | (probe q1) | |
| volatile スカラ/ポインタの値（`volatile int`, `*p+=`） | ✅ | (probe u3) | 値の正しさのみ。順序保証は別 |
| 文字/エスケープリテラル（`\n` `\0` `\x41` `\101`） | ✅ | (probe u4) | |
| char/short 実引数の int 昇格（可変長経由） | ✅ | (probe u5) | |
| 非可変長の char/short/float 実引数昇格 | ✅ | (probe prom_check) | float→double 昇格含む |
| `restrict` 修飾ポインタ（値の正しさ） | ✅ | (probe restrict_check) | |
| `_Atomic` キーワード（`_Atomic int`/`long` の値） | ✅ | (probe atomic2) | stdatomic.h は同梱なし |
| ワイド文字/文字列リテラル `L'A'` `L"hi"[i]` | ✅ | (probe wide2) | int/long 値として動作 |
| stddef.h の wchar_t / max_align_t（C11 7.19） | 🔧 | stddef_wchar_t, stddef_max_align_t | wchar_t=int(4B), max_align_t=long double(8/8) |
| 戻り値の暗黙変換（int↔double, char/short/unsigned 切り詰め） | ✅ | (probe ret_conv, ret_conv2) | |
| VLA（1D / 2D / 関数引数 / sizeof 全体） | ✅ | (probe vla1, vla2, vla4) | |
| VLA を subscript した `sizeof(a[0])` | 🔧 | sizeof_vla_subscript | `sizeof(a)` 全体は元から動作 |

---

## バグではない（仕様 / 既知の差異、追わない）
- statement expression `({...})`（GNU 拡張、非標準）。
- 過剰初期化子 `struct S s={{1,2},{3,4}}`（メンバ1個に2グループ）等は ag_c は意図的に E3064（厳格）。
- `s07.c`（深さ 10 万の再帰）の SIGSEGV はスタックオーバーフロー（誤コンパイルではない）。
- 評価順序が未規定/UB のもの（`a[i++]=i` 等）。
- 同梱ヘッダに `complex.h` が無く `#include <complex.h>` が E1034（`_Complex` 言語機能自体は動く）。
- 同梱ヘッダに `stdatomic.h` が無く `#include <stdatomic.h>` が E1034（`_Atomic` 言語機能自体は動く）。

## 未チェックの着手候補（⬜ を埋める）
- `_Generic` 深いネスト型の同一型マッチ（上表 ⚠️、複雑型 assoc 照合の改善）。
- 複数 translation unit / extern リンケージの差分（現状の差分テストは単一ファイル）。
- 浮動小数の `printf` 出力書式（%f/%g/%e の丸め）。

## チェック済みだが miscompile でなかった領域（再探索不要）
- compound literal: ネスト / `&(compound)` / 配列 / 関数引数 (probe cl1, cl3)。
- `static p = &(struct){..}`（非定数 static init）は clang が拒否、ag_c は受理（寛容な差異）(probe cl2)。
