# 差分テスト バグカバレッジ表

clang との差分テスト（同一 C ソースを ag_c と clang でコンパイルして exit code を比較）で
炙り出した miscompile / コンパイルエラーの **チェック済み領域** を管理する。同じ領域を
何度も探さないための索引。

最終更新: 2026-06-30（struct funcptr designated zero-init まで）

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
| unsigned char/short 戻り値の整数昇格（比較の符号） | 🔧 | unsigned_subint_return_promote | funcall に is_unsigned を立てていたため `f()>-1` が unsigned 比較で誤判定。char/short は signed int 昇格なので除外、unsigned int/long のみ保持 |
| typedef 経由 unsigned char/short (`uint8_t`) のロード符号性 | 🔧 | stdheader/stdint_uint8 | adjust_local_decl_spec_from_typedef が typedef の unsigned を上書きして捨て、1byte ロードが ldrsb に。200→-56 (exit truncation で偽装通過) |
| typedef 経由 unsigned char/short の**戻り値**ゼロ拡張 | 🔧 | typedef_unsigned_subint_return | resolve_func_ret_typedef が typedef の unsigned を捨て、sub-int 戻り値が符号拡張 (`u8 f(){return 200;}`→-56)。上記ローカルと同根の戻り型版 |
| typedef unsigned を struct **メンバ**型に使ったときのロード符号性 | 🔧 | typedef_unsigned_struct_member | struct_layout の typedef メンバ分岐が td_isu を member_is_unsigned に反映せず符号拡張 |
| unsigned char/short **配列メンバ** `s.x[i]` のゼロ拡張 | 🔧 | unsigned_char_array_member | build_member_deref_node が配列メンバに pointee_is_unsigned を立てず（is_bool と非対称）。サイズ1配列は array_len=0 に潰れるため subscript で base is_unsigned も伝播 |
| `unsigned long`/`unsigned char` 戻りの符号性追跡 | ✅ | unsigned_long_return_signedness | 旧 ⚠️。再検証で shift/divide/比較いずれも clang 一致＝再現せず。回帰 fixture 化 |
| 混在幅比較（片側 i32・片側 i64） | ✅ | mixed_width_comparison | 旧 ⚠️。混在は 64bit で sign/zero-extend して比較され正しい（miscompile でない）。回帰 fixture 化 |
| 複合代入の sub-int / unsigned wrap | ✅ | (probe p3) | |
| 連鎖代入 `b=a=E` の sub-int lvalue 経由の値変換 | 🔧 | chained_assign_narrow_lvalue | 代入式の値が lvalue 型へ変換されず、`b=a=300`(a=uchar) で b が 300 のまま。格納後に再ロードして返す (lvar/gvar/deref/member) |
| 三項 `c?int:char` の sub-int 分岐の値 | 🔧 | ternary_subint_branch | char 分岐を 4 バイト slot に strb で書き上位 garbage→merge ldrsw が誤値。結果型へ retag して full-width store (signed/unsigned 双方正しい) |
| シフト境界・unsigned/signed 右シフト | ✅ | (probe p7) | |
| **シフト結果型 = promoted left operand** `((unsigned)1 << (long)1)` | 🔧 | shift_left_operand_type | C11 6.5.7p3。結果型を左右最大幅にしていたため右辺 `long` で結果が 64bit 化し c-testsuite 00200 が不一致。ND_SHL/ND_SHR の型幅・unsigned 伝播・IR result_ty を promoted 左辺基準へ。併せて `(long)1` / `(unsigned long)1` の cast 結果幅を保持し `sizeof((long)1)==8` を固定 |
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
| 負値の浮動小数点グローバル初期化子 `float g=-1.0f` | 🔧 | negative_fp_global_init | psx_eval_const_fp が ND_FNEG 未対応で畳み込み失敗→スカラ .comm(0)/配列要素 0。ND_FNEG 畳み込み＋fp配列要素の非ND_NUM評価を追加 |
| 同梱 `<complex.h>` (I/creal/cimag/conj/cabs/carg、算術) | 🔧 | stdheader/complex_ops | _Complex brace 初期化・複素数 compound literal で I={0,1}、creal/cimag は __real__/__imag__ ベース (rvalue 可)、cabs/carg は <math.h> 経由。算術はネイティブ。値渡し無し |
| GNU `__real__` / `__imag__` 演算子 | 🔧 | real_imag_operators | 複素数式を temp slot に materialize し re/im を fp load。実数は x/0。rvalue 可 |
| `double _Complex` ↔ `float _Complex` 変換 | 🔧 | complex_float_double_convert | build_complex_to が成分の fp 種別を無視して memcpy し虚部が壊れていた。源 fp で materialize→各成分 F2F 変換 |
| `_Complex` の値渡し / 値返し ABI | 🔧 | complex_by_value_abi | HFA として re→d0/s0, im→d1/s1 で受渡し。param 受取(2 IR_PARAM)・return・呼出引数(2 FP 展開)・戻り値受取(d0/d1→slot) を実装。double complex はポインタ渡し、float complex は 1 レジスタで虚部喪失していた |
| 複素数初等関数 cexp/clog/csqrt/cpow/csin/ccos/ctan 等 | 🔧 | stdheader/complex_ops | 値渡し/値返しが効くので static 関数で実装。実部/虚部は <math.h> 経由。libm 同名を static で隠し ag_c/clang 同一実装で一致 |
| 本物の `<stdatomic.h>` (LSE アトミック命令) | 🔧 | stdheader/stdatomic_ops | 単一スレッド退化版から、Apple ARM64 LSE 命令 (ldaddal/ldsetal/ldclral/ldeoral/swpal/casal/ldar/stlr/dmb ish) による真のアトミックへ。fetch 系は規格通り旧値を返す。__ag_atomic_* 組込み + IR_ATOMIC + 1/2/4/8 バイト codegen。全 seq_cst 強度 |
| **fp 実引数 → 整数仮引数** の暗黙変換 `ei(7.9)` (直接呼出) | 🔧 | fp_arg_to_int_param | C11 6.5.2.2p7。ag_c は double/float 実引数を fp レジスタ(d0)に置いたまま callee を呼び、callee が整数レジスタ(x0/w0)から読みゴミ値に。callee の整数スカラ仮引数幅 (4/8) を semantic_ctx に記録 (param_int_sizes、既存 param_fp_kinds と排他) し、呼び出し側 build_node_funcall で実引数が fp なら F2I(fcvtzs) を挿入。long 仮引数は i64 幅で変換 (i32 だと大値が wrap)。同一 TU 定義関数で有効。**符号性は fcvtzs 固定** (fp→unsigned の大値は既存 F2I の制約) |

### 関数ポインタの FP 戻り値（`d0` で読む）
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| funcptr 変数 / 仮引数 / typedef / `(*p)()` | 🔧 | funcptr_fp_return, 0b980b0 | |
| 関数ポインタ配列の要素 `ops[i]()` | 🔧 | funcptr_array_fp_return, 45bd478 | |
| struct メンバ funcptr `s.f()` / `sp->f()` | 🔧 | funcptr_member_fp_return, 20c4b17 | nested struct / struct-array member (`h.ops[i].f`, `h.p->f`) も追加確認 |
| struct の先頭 aggregate funcptr メンバと `{0}` 初期化 | 🔧 | struct_funcptr_zero_init, struct_funcptr_designated_zero_init, nested_struct_funcptr_designated_zero_init | `struct Holder h = {0};` で先頭メンバが `struct Ops ops[2]` のとき、明示 `0` が 16B aggregate 代入として IR に渡り E4007。struct 全体の pre-zero-fill 後、terminal `0` は no-op として消費し、synthetic lvar の `tag_kind` 既定値も `TK_EOF` に初期化。designator 経由の `.ops={0}`、nested struct/union designator、global/static local も Wasm object scan 対象 |
| グローバル funcptr `gops()` | 🔧 | funcptr_global_fp_return, ada7696 | |
| グローバル funcptr **配列** `gops[i]()` の fp 戻り (N>=2) | 🔧 | funcptr_global_array_fp_return, e62862e | |
| 要素数 1 の括弧内配列グローバル `(*g[1])()` / `(*g[1])` | 🔧 | global_size1_funcptr_array | paren 内 `[1]` の有無で配列判定。funcptr/ポインタ両方 |
| 間接呼び出しの int→fp 引数昇格 (直書き funcptr) `fp(3)` | 🔧 | funcptr_int_to_fp_arg | 宣言時に skip_func_params で各仮引数の fp 種別を funcptr_param_fp_mask に記録し、parse_call_postfix で fp 仮引数の実引数を wrap_to_fp(ND_INT_TO_FP) でラップ。float/double/混在/(*fp)() 対応 |
| **typedef 関数ポインタ経由の int→fp 引数昇格** `typedef double (*Op)(double); Op op; op(3)` | 🔧 | typedef_funcptr_int_to_fp_arg (続き107) | 直書き funcptr と違い typedef に仮引数 fp マスクを保存せず、整数実引数を x0/w0 に置いたまま間接呼び出ししていた。`psx_typedef_info_t` と local/global funcptr 変数に mask を伝播し、`ND_LVAR` / `ND_GVAR` 呼び出しで wrap_to_fp。複数 typedef が続いた後に直近 typedef の stale mask が漏れないよう、直書き宣言子だけ `psx_last...` を優先 |
| **struct メンバ関数ポインタ経由の int→fp 引数昇格** `ops.f(3)` / `ops.arr[i](3)` | 🔧 | funcptr_member_int_to_fp_arg (続き108) | callee が `ND_DEREF` になるため local/global funcptr の mask 参照に乗らず、整数実引数を x0/w0 に置いたままだった。tag member と `node_mem_t` に funcptr_param_fp_mask を伝播し、`ND_DEREF` callee でも wrap_to_fp。併せて brace 初期化で funcptr メンバの戻り fp_kind をメンバ自身の FP 型と誤解し、関数アドレスを double 化して格納していた問題を修正。直書き/typedef/配列メンバ・`.`/`->` を網羅 |
| **関数ポインタ経由の fp 実引数→整数仮引数変換** `fp(7.9)` / `ops.f(7.9)` | 🔧 | funcptr_fp_to_int_arg (続き109) | 直接呼び出しは `param_int_sizes` で F2I していたが、間接呼び出し用の funcptr 型には整数仮引数幅を保存しておらず、FP 実引数を d0/s0 に置いたまま callee が x0/w0 を読んでいた。`funcptr_param_int_mask` を typedef/local/global/tag member/`node_mem_t` に伝播し、`ND_LVAR` / `ND_GVAR` / `ND_DEREF` callee で FP 実引数を `ND_FP_TO_INT` にラップ。直書き/typedef/struct メンバ/配列要素、int/long、FP 戻りとの混在を網羅 |
| 可変長プロトタイプの無名固定引数 `int printf(const char*,...)` | 🔧 | variadic_unnamed_proto_fixed_args | 固定引数数 0 誤算→crash。定義なし外部関数で顕在化 |
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
| **配列へのポインタ経由の struct メンバ** `struct S (*ap)[N]; (*ap)[i].m` | 🔧 | ptr_to_array_struct_member | `(*ap)[i].m` の `.` が E3005 で弾かれていた。ap (lvar) は tag_kind=STRUCT を持つが is_tag_pointer=0 (ポインタ-to-配列であって tag ポインタでない) のため、build_unary_deref_node 冒頭の `is_tag_ptr` ガードで tag が deref ノードに carry されない。subscript 段で psx_node_get_tag_type が TK_EOF を返し member access が E3005。outer_stride+!is_array 経路で tag を carry するよう修正 (is_tag_pointer=0: 結果は配列、要素が struct)。struct 値コピー `s=(*ap)[1]` (元から動作)、メンバ read/write・仮引数経由・2D `(*ap2)[i][j].m`・union 要素 `(*up)[i].s.a`・グローバル `gap` を網羅 |
| **struct メンバ `int (*p)[N]` (配列へのポインタ)** `struct H { int (*p)[3]; }` | 🔧 | struct_ptr_to_array_member (続き70) | parser が struct メンバ宣言子 `int (*p)[N]` を `int *p[N]` (配列要素はポインタ) と区別できず両方 paren_array_mul=1, is_ptr=1, trailing-array=N として解析。`sizeof(struct H)` が 8 ではなく 24 (3 ポインタ分の誤レイアウト)、`(*g_h.p)[i]` が SIGSEGV (`*g_h.p` を int 1 個 load → スカラ + i deref)。修正: member_decl_head_t に ptr_in_paren を追加し parse_member_decl_name_recursive で `(` 内 `*` 消費を持ち回す。ptr_in_paren=1 && paren_array_mul=1 のとき trailing `[N]` は pointee dim としてメンバ自身は単一ポインタ (8B)、pointee 全バイト数 (N*elem) を tag_member_info.outer_stride に保存。build_member_deref_node に pointer-to-array 分岐 (is_tag_pointer && outer_stride>0 && array_len==0) を追加しローカル `int (*p)[N]` lvar と同じ表現 (deref_size=outer_stride、inner_deref_size=elem) で ND_DEREF を組む。build_unary_deref_node に ND_DEREF operand 用の対応分岐 (is_tag_pointer && inner_deref_size>0 && deref_size>inner_deref_size) を追加し新 deref の deref_size を elem に再設定 → subscript_base_address_of が lhs を返し `(*s.p)[i]` がポインタ load+elem ストライド add で解決。グローバル/ローカル struct、read/write、`->` 経由を網羅 |
| **struct メンバ `int (*p)[M][N]` (2D pointee の pointer-to-array)** `struct H { int (*p)[2][3]; }` | 🔧 | struct_ptr_to_2d_array_member (続き72) | 続き70 の延長。sizeof は正しかったが pointee の dims 情報 (M, N) が outer_stride に「全バイト数」しか保存されておらず、`(*g_h.p)[i][j]` の 1 段目 subscript で elem 段の stride になり SIGSEGV/誤値。修正: struct_layout.c で pointee_arr_dim_count / pointee_arr_first_dim を保存し、2D pointee のとき psx_ctx_set_tag_member_mid_stride で mid_stride (= N*elem) も保存。build_member_deref_node の pointer-to-array 分岐で mem_info->mid_stride>0 のとき deref->inner_deref_size に mid_stride、deref->next_deref_size に elem を置きローカル `int (*p)[M][N]` lvar 表現と整合。build_unary_deref_node の続き70 分岐 (ND_DEREF operand) で probe->next_deref_size>0 を見て結果 deref の inner_deref_size に carry。グローバル/ローカル struct、read/write、`->` 経由を網羅 |
| **struct メンバ `int (*p[M])[N]` (array-of-pointer-to-array)** `struct H { int (*p[2])[3]; }` | 🔧 | struct_array_of_ptr_to_array_member (続き71) | 続き70 の延長。parser が `int (*p[M])[N]` を `int *p[M*N]` と区別できず arr_size=M*N として解析、sizeof が 8*M*N (M=2,N=3 で 48) と誤レイアウト、`(*g_h.p[i])[j]` も SIGSEGV/誤値。修正: struct_layout.c の ptr_in_paren && paren_array_mul>1 分岐を追加し arr_size=M, pointee 全バイト数 (N*elem) を新フィールド `ptr_array_pointee_bytes` に保存 (tag_member_info_t / tag_member_t / node_mem_t に追加、setter / fill / 匿名 struct 昇格伝播を追加)。build_member_deref_node の array_len>0 && is_tag_pointer 分岐で deref に carry。build_subscript_deref に新分岐: base ND_DEREF.ptr_array_pointee_bytes>0 のとき結果 deref を single pointer-to-array 形 (is_tag_pointer=1、deref_size=ptr_array_pointee_bytes、inner_deref_size=elem、pql=0) に組み直し、続き70 の build_unary_deref_node の pointer-to-array 分岐に乗せる。グローバル/ローカル struct、read/write、`->` 経由、`int *q[M]` との区別保持を網羅 |
| **ローカル `int (*p[M])[N]` (array-of-pointer-to-array)** | 🔧 | local_array_of_ptr_to_array | struct メンバ版の `ptr_array_pointee_bytes` が lvar 側になく、`int (*p[2])[3] = {a,b}; p[0][0][0]` が配列スロット内のポインタ値ではなくスロット自身を基点にして誤値。lvar_t に ptr_array_pointee_bytes を追加し、識別子参照/配列 decay へ伝播。`build_subscript_deref` は中間行では ptr_array_pointee_bytes/base_deref_size を carry、最終次元では pointer-to-array 値として組み直す。1D/2D 配列、direct `p[i][j][k]`、explicit `(*p[i])[j]`、書き込みを網羅 |
| **typedef chain で基底が配列の場合の dims 合成** `typedef int Row[3]; typedef Row Matrix[2]` | 🔧 | typedef_array_chain | Matrix が int[2] として登録され sizeof(Matrix)=24 のはずが 8、`Matrix m={{1,2,3},{4,5,6}}` も E3064。トップレベル/関数内の両方で base typedef の array_dims (= [3]) と declarator の dims (= [2]) を [declarator..., base...] の順で結合するよう修正 (parser.c の define_toplevel_typedef_from_declarator と stmt.c の parse_typedef_decl)。**ついでに**: stmt.c parse_typedef_decl が通常の配列 typedef `typedef int Row[3]` でも is_array=1 を立てておらず (トップレベル parser.c とは非対称)、関数内 `Row r={1,2,3}` が E3064 で弾かれていた回帰も同時に修正 (is_plain_array 分岐を追加)。3 段 chain (A→B→C)・基底が多次元 (`typedef int M23[2][3]; typedef M23 M4[4]`)・declarator が多次元 (`typedef Row Cube[2][5]`)・グローバル変数・flat init・関数内 chain を網羅 |
| **グローバル多次元 pointee** `(*pa)[N][M]` | 🔧 | global_ptr_to_multidim_array, eb74293 | int/double, 4D まで |
| 変則形 `*(t+N)[K]`（pointer-to-array に subscript+deref 混在） | 🔧 | ptr_array_arith_subscript_deref | `*((t+N)[K])`。make_subscript_scaled_offset に ND_ADD/ND_SUB 分岐を追加し、ポインタ算術 base からポインタ被演算子の inner_deref_size 等を引き継ぐ。これがないと `(t+N)[K]` が配列へ decay せずスカラ load→外側 `*` が値をアドレス deref して SIGBUS。スカラポインタは inner=0 で無影響 |
| typedef 配列へのポインタの stride `typedef int R[3]; R *p` (1D/2D) | 🔧 | typedef_array_pointer_stride, typedef_pointer_element_array_sizeof | is_pointer+td_array 分岐で outer_stride/mid_stride を設定。pointer_qual_levels を立てない (= 多段ストライド連鎖を使う) ことで 2D `m23 *q; q[i][j][k]` の深い添字も正しく動く。`typedef BinOp OpArr3[3]; OpArr3 *pa; (*pa)[i](...)` は typedef の要素関数ポインタ分まで pointer level に数えて分岐から外れ、関数コードを int として読んで SIGBUS していたため、配列 typedef 自体に宣言子 `*` を足した形も stride 分岐に入れる |
| typedef 自体が配列へのポインタ `typedef int (*PA)[3]; PA p` (局所使用) | 🔧 | typedef_ptr_to_array | 上の `R *p` と別経路。typedef 定義 (toplevel: parser.c / 関数内: stmt.c) が is_ptr のとき括弧の後ろの `[3]` (ポインティ extent) を捨てていて、`p+1`/`p[i]` が 1 行でなく要素 1 個 (4B) しか進まなかった。`*` が括弧内 (ptr_in_paren) のとき `[3]` をポインティ dims として記録し、resolve_typedef_array_dims の is_array ゲートを外して宣言側の `is_pointer && td_array_dim_count>0` 分岐に乗せる。多次元ポインティ `(*PB)[2][3]` も対応 |
| ポインタ typedef を基底にした**グローバル変数** `typedef int *PI; PI gp` | 🔧 | global_pointer_typedef | gp が int スカラ登録 (sizeof=4, `gp[i]` で E3064)。parse_toplevel_decl_after_type のオブジェクト経路が base_is_ptr を 0 固定で宣言子へ渡し、typedef 基底のポインタ性を捨てていた (typedef 経路は渡していた)。`g_toplevel_decl_base_is_ptr` を渡すよう修正。int/char/long/unsigned/double*・struct ポインタ・pointer-to-array (PA/PB) を網羅。double は pointee fp_kind を実効段数で判定して伝播、pointer-to-array typedef グローバルは typedef のポインティ dims から outer_stride を設定 (直書き `int *gp`/`int(*gp)[3]` は宣言子の `*` で立つので不変) |
| 多段ポインタ typedef `typedef int **PP; PP p` (局所/仮引数) | 🔧 | multilevel_pointer_typedef | typedef がポインタ段数を bool でしか持たず `**p` が誤 deref→SIGSEGV。typedef_name_t に pointer_levels を追加し、定義時 (toplevel: parser.c / 関数内: stmt.c・decl.c) に「基底段数+宣言子 prefix `*` 数」を 2 段以上だけ記録、宣言時 (decl.c) に getter で取得して total_pointer_levels/pql に反映。直書き `int **p` と同一ノード属性になる。2/3 段・合成 `typedef PI *PP2`・仮引数を網羅 |
| グローバル変数の多段ポインタ `int **gp; **gp` (直書き/typedef経由) | 🔧 | global_multilevel_pointer | 第1 deref `*gp` が int* (8B) でなく int (4B, ldrsw) ロードされ、続く deref が壊れた値を deref→SIGSEGV。register_toplevel_global_decl がポインタ deref_size を常に要素サイズにし、global_var_t が段数を持たなかった。global_var_t に pointer_qual_levels を追加 (宣言子 `*` 数 + 基底ポインタ typedef 段数)、try_build_global_var_node が pql>=2 のとき参照ノードに deref_size=8/base_deref_size=要素/pql を立てローカル `int **lp` と同一表現に。int/char/struct・3段・`(*gp)[i]` 添字・`*gp=` 代入を網羅 |
| 多段ポインタの fp pointee `double **p; **p` / `(*pp)[i]` 配列 decay 添字 | 🔧 | multilevel_pointer_fp_pointee, global_multilevel_pointer | fp_kind が多段ポインタへ伝播せず float がゴミ・double 書き込みが落ちていた。(1) 宣言時に多段ポインタへも最内 pointee_fp_kind を設定 (旧: total_pointer_levels==1 のみ)、(2) build_unary_deref_node / build_subscript_deref が pql を 1 段消費するとき pointee_fp_kind を結果へ引き継ぐ。続き223で global `double **gdp` / typedef `DPP gtdp` も修正: top-level のデータポインタ pointee_fp_kind 保存が実効段数 1 に限定されていたため、global 多段だけ最内 double 印を落としていた。double/float の read/write・3段・`*pp[i]`・`(*pp)[i]`・global 直書き/typedef を網羅 |
| struct 配列の部分/0/designator 初期化の 0 補完 | 🔧 | struct_array_partial_init | 部分初期化された struct 要素の残メンバが 0 補完されず garbage / ネスト struct 配列が ir_build 失敗 / `[i].field=` が E2006。配列全体を先に 0 埋め+明示初期化子で上書き、`[i].member` designator もパース対応 |
| **グローバル struct 配列の flat brace elision + 未完了 `[]` 次元推論** `struct PT{long c[4];long b,e,k;} cases[]={1,2,...}` | 🔧 | global_struct_array_flat_elision | c-testsuite 00205。psx_gbrace_flat が配列 level の positional 初期化で scalar 1 個ごとに struct 要素境界へ揃えていたため、`c[1]` 以降が 0 になっていた。境界揃えは braced subinitializer の直後だけに限定。さらに `T a[]` の type_size 推論が flat scalar slot 数を外側要素数として扱い、`sizeof(cases)/sizeof(cases[0])` が過大になって余分な 0 要素を出力していたため、struct 要素は `global_flat_slot_count` で割り上げて外側次元を推論し末尾 slot を 0 補完。 |
| sized array 複合リテラルのアドレス `&(int[N]){...}` | 🔧 | addr_of_array_compound_literal | build_unary_addr_node の COMMA 分岐が rhs(既に ADDR)を二重ラップ→ir_build失敗。rhs に & ロジックを再帰適用 |
| 多次元/typedef 配列複合リテラルのアドレス `&(int[N][M]){...}` / `&(Row){...}` | 🔧 | addr_of_array_compound_literal | cast parser が raw `[N][M]` の 2 次元目を読まず、typedef 配列 (`typedef int Row[3]`) は配列情報を compound literal へ渡していなかった。さらに `&` 後に pointer-to-array の stride が 1 段ずれず、内側 subscript の幅が落ちる穴があった。cast type から dims を渡し、匿名 lvar/global に outer/mid/extra stride を設定、`&` で deref/inner/next をシフトするよう修正。raw 2D、typedef 1D/2D、struct 配列 typedef を網羅 |
| ファイルスコープのスカラ複合リテラルのアドレス `int *p=&(int){5};` | 🔧 | file_scope_addr_of_compound_literal | B6。ファイルスコープのスカラ複合リテラルが値 (ND_NUM) に短絡され `&` がアドレスを解決できず `.comm`(0)→`*p` が NULL deref で SIGSEGV。C11 6.5.2.5 で静的記憶域なので、`&` 配下のとき (g_addr_of_compound_pending) は gvar 実体を生成しアドレス初期化。int/long/char/unsigned/double/float・ポインタ配列要素を網羅。**根因の副次バグも修正**: 関数プロトタイプ/定義後に g_current_funcname を NULL に戻しておらず、`<assert.h>` の `__assert_rtn` 宣言後にファイルスコープ複合リテラルが「関数内」と誤判定されローカル lvar 経路に乗っていた |
| ファイルスコープの **struct/配列** 複合リテラルのアドレス `&(struct S){...}` | 🔧 | file_scope_aggregate_compound_literal_addr | ファイルスコープ分岐が単一スカラ (psx_expr_assign 1 個) しか扱えず `,` で E2006。集約 (struct/union/配列) のとき gvar 実体を作り、グローバル struct/配列と同じ psx_parse_global_brace_init_flat で brace 初期化を展開してアドレス可能なノードを返す分岐を追加。init_fvalues も確保して fp 配列に対応。struct/designator/ネスト/union/char配列/int・double 配列を網羅 |
| 大きいスタックフレーム（>4095B）の `sub sp`/`add x,x29,#off` | 🔧 | large_stack_frame | imm12 上限超を 4096 倍数部(lsl#12)+端数に分割。続き94で長大 00200 により vreg spill の `ldr/str [x29,#off]` が 32KB 超でも失敗することが露出したため、frame load/store helper で範囲外は `add x16,x29,#off; ldr/str [x16]` にフォールバック |
| ポインタ減算・比較 | ✅ | (probe p6) | |
| **ポインタ戻り関数の subscript/算術** `g()[i]` / `*(g()+i)` | 🔧 | func_pointer_return_subscript | parser がポインタ戻り値の pointee 型を覚えず ND_FUNCALL の deref_size=0。subscript/算術がスケールせず 1 バイト加算で miscompile (`int* g(); *(g()+3)` が 5120)、`double* g()` は戻り値を d0 から誤読し SIGSEGV、`unsigned char* g()` は符号拡張。修正: (1) semantic ctx の戻り値型 (tag/token_kind) から pointee サイズ/fp 種別を導出し ND_FUNCALL の deref_size/type_size/pointee_fp_kind アクセサに反映、(2) ポインタ返しの fp_kind を funcall ノードに立てない (戻り値は x0)、(3) 基底型 unsigned を ctx に保存し subscript の zero-extend に伝播 (戻り値そのものの符号化は is_pointer でガード)。int/long/char/unsigned char/short・double/float・struct(.member/->)・void* cast・ポインタ算術 (+/-)・変数添字を網羅 |
| **配列へのポインタを返す関数** `int (*f())[N]` の `f()[i][j]` | 🔧 | func_return_pointer_to_array | parse_func_declarator が pointee 配列次元 `[N]` を読み飛ばし、call ノードに行ストライド (N*elem) が無く base 要素 (4) で誤スケール→SIGSEGV (`int(*p)[N]=f()` 経由は動作)。semantic ctx に先頭次元 N を記録 (ret_pointee_array_first_dim)、ps_node_deref_size(ND_FUNCALL)=N*elem、subscript inner_ds=elem、build_unary_deref_node が `*f()` を要素サイズで解決。f()[i][j]・(*f())[j]・(*(f()+k))[j]・write・引数あり・double を網羅 |
| **配列へのポインタを返す関数の 2D pointee** `int (*f())[N][M]` の `f()[i][j][k]` | 🔧 | func_return_pointer_to_2d_array (続き111) | 続き19 の直接関数版を多次元 pointee へ拡張。先頭次元 N だけでは `f()[i]` の stride が N*elem になり、実際に必要な N*M*elem / M*elem / elem を carry できず SIGSEGV。parse_func_declarator が第2次元 M も ctx に記録し、ps_node_deref_size(ND_FUNCALL)=N*M*elem、subscript 結果へ M*elem と elem、`*f()` へ inner_deref_size を伝播。int/double、read/write、`(*f())[j][k]`、`(*(f()+i))[j][k]`、引数ありを網羅 |
| **配列へのポインタを返す関数ポインタ** `int (*(*fp)())[N]` の `fp()[i][j]` | 🔧 | funcptr_return_pointer_to_array (続き110) | 続き19 の間接呼び出し版。`ND_FUNCALL.callee != NULL` では関数名 ctx を引けず、callee 側に戻り値の pointee 配列次元/要素サイズが無いため `fp()[i][j]` が E3064 または誤スケール。さらに `double (*(*fp)())[N]` では callee の pointee_fp_kind を「戻り値が double」と扱い、実際はポインタ戻り値なのに d0 から読んで SIGSEGV。修正: function pointer 型に funcptr_ret_pointee_array_first_dim/elem_size を保存し、local/global/typedef/struct member から ND_FUNCALL indirect・subscript・`*fp()` へ伝播。pointer-to-array 戻りでは fp_kind を funcall 戻り値に立てず、要素 fp 種別として subscript に渡す。直書き・typedef・global・struct member、int/double、write、`(*fp())[j]`、`(*(fp()+k))[j]` を網羅 |
| **配列へのポインタを返す関数ポインタの 2D pointee** `int (*(*fp)())[N][M]` の `fp()[i][j][k]` | 🔧 | funcptr_return_pointer_to_2d_array (続き112) | 続き110 の間接呼び出し版を多次元 pointee へ拡張。function pointer 型が first_dim/elem_size しか保持せず `fp()[i]` の stride が N*elem になって SIGSEGV。funcptr_ret_pointee_array_second_dim を typedef/local/global/tag member/node_mem_t へ伝播し、ND_FUNCALL(callee) の deref_size/subscript/`*fp()` で N*M*elem / M*elem / elem を carry。直書き global/struct member では trailing `[N][M]` をオブジェクト配列ではなく戻り pointee 次元として登録。int/double、read/write、`(*fp())[j][k]`、`(*(fp()+i))[j][k]`、引数あり、typedef/global/struct member を網羅 |

### 集約初期化（C11 6.7.9）
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| 多次元配列メンバ / ネスト designator | 🔧 | 4b92768, aadf3b7 | |
| 重複 designator 後勝ち / 位置継続 | 🔧 | 4a5942d, df23e17 | C11 準拠 |
| struct 配列メンバ brace init | 🔧 | 1684a8c | |
| グローバル designator `.member[idx]`/`.member.sub` | 🔧 | global_designator_member_index, 1e843b4 | |
| ローカル designator の struct/union leaf brace init | 🔧 | local_designator_aggregate_leaf, 7e39081 | union leaf (`.arr[1] = {.n=7}`) は旧コメントで除外されていたが、union 配列要素 brace init 側が修正済みになったため同じ nested designator leaf 経路で `parse_union_initializer` へ委譲するよう拡張 |
| `_Bool` 初期化子の 0/1 正規化（全経路） | 🔧 | 5b3d592 | |
| グローバルポインタ配列 `&data[n]`/`data+n` | 🔧 | global_ptr_array_addr_init, 138cd70 | |
| >8B struct の複合リテラル代入 `s=(struct S){...}` | 🔧 | compound_literal_struct_assign | ir_build_module failed。build_assign_struct が src に ND_COMMA(init,temp) 形を扱わず fail。init 評価後 temp を struct ソースに |
| >8B struct を返す関数呼び出しを分岐に持つ ternary 初期化/代入 `struct B x = c ? f() : g()` | 🔧 | struct_init_from_ternary_funccall | <=8B は既に ND_ASSIGN(var, ternary) で動いていたが、>8B は parser が E3064 で拒否していた。ternary の型サイズが代入先 struct サイズと一致する場合は ND_ASSIGN にし、IR の materialize_aggregate_expr_to が各分岐を dst へ materialize する経路に乗せる。funccall/funccall、funccall/lvar、初期化、既存変数への代入を網羅 |
| 2D char 配列の文字列リスト初期化 `char a[2][6]={"hello","world"}` | 🔧 | char_2d_array_string_init | 各文字列がスカラ/ポインタ扱いで行を埋めず壊れる。ローカル/グローバル両経路で文字列を行 (row 幅) のバイト列に展開し残り 0 埋め |
| union 配列要素の brace init `arr[2]={[1]={.n=5}}` / struct メンバ union 配列 `.u={[1]={.n=7}}` | 🔧 | union_array_brace_init | 旧 ⚠️(E3064)。E3064 は解消済みだが今度は値が格納されず 0 に化けていた (miscompile)。parse_array_elem_struct_brace_init が要素を常に parse_struct_initializer へ投げ、union 要素の `.n=5` を struct レイアウトで誤解決していた。続き216で struct メンバ配列版も同根と判明: parse_member_initializer が union 配列要素を parse_struct_initializer へ投げ、`.n=7` 後に未指定 `.l=0` が同じ offset を上書きしていた。要素が union のとき parse_union_initializer へ委譲。続き217で global struct メンバ union 配列の ARM64 data emitter padding も修正: active union メンバ出力後に union 要素サイズまで 0 padding せず、後続要素/tail が前へ詰まっていた。designator/positional/混在メンバ/部分初期化/local+global struct メンバ union 配列を網羅 |
| **ローカル struct メンバ多次元 struct タグ配列の designator init** `struct Grid g = {.rows={[2]={[1]={.val=99}}}};` | 🔧 | local_struct_member_multidim_nested_designator (続き76/218) | 続き75 のローカル経路。関数内で designator (`[N]=`, `[M]=`, `.member=`) が「E3064: [primary] 数値が必要です」で拒否されていた。parse_member_initializer の outer_stride 経路が designator を全く扱わず、`[` を見ると parse_scalar_brace_initializer に投げて失敗。修正: 外側 brace 開始時に `[N]=` 検出で flat=N*inner_len、内側 brace で `[M]=` 検出で k=M、要素 tag が struct/union のとき nested lvar (offset/size/tag を 1 slot に絞る) で parse_struct_initializer を呼んで `.member=` も解決。続き218で 3D 以上も修正: 中間 brace を struct 要素として早く解釈せず、次元ごとに再帰して最下層だけ struct/union initializer に委譲。単一フィールド struct の positional は既存挙動を維持 |
| **グローバル struct メンバ多次元 struct タグ配列の内側 brace の designator** `g = {.rows={[2]={[1]={.val=99}}}}` / `g = {.rows={[2]={{.val=99}}}}` | 🔧 | global_struct_member_multidim_nested_designator (続き75) | 続き74 の延長。内側 brace で `.val=` や `[1]=` を使うと E3064。原因: parser.c の gbrace_child_at が sub_dims を 1 段消費する処理を `ctx.tag_kind == TK_EOF` 限定としていたため struct タグ多次元配列の内側 brace ctx が「単一 struct」になり、内側 designator が解決できなかった。修正: gbrace_child_at の sub_dims 処理を tag_kind 非依存にし、struct タグ配列でも中間段/最内 1 段で「内側次元の配列 (タグ継承)」ctx を返す。2D/3D struct タグ配列・`.member=` designator・`[N]=` designator・部分初期化を網羅。ローカル版も local_struct_member_multidim_nested_designator で対応済み |
| **グローバル struct メンバ 2D struct タグ配列の外側 `[N]=` designator** `struct S { struct C rows[3][2]; } g = {.rows={[2]={{99},{100}}}}` | 🔧 | global_struct_member_multidim_struct_array_designator (続き74) | 99/100 が rows[2] でなく rows[1] に書かれていた。3 経路の合わせ技: (1) struct_layout.c の arr_dims 保存条件 `member_tag_kind == TK_EOF` がタグ配列メンバを除外。修正: `!member_is_ptr` に緩和し struct タグ配列も保存。(2) parser.c gbrace_ctx_from_member の `mi->tag_kind == TK_EOF` 条件を `mi->arr_ndim >= 2` に緩和、struct タグ多次元配列も sub_dims を carry。(3) psx_gbrace_flat の `[N]=` elem_slots 計算で tag_kind==TK_STRUCT 経路でも sub_dims の積を掛けて内側次元を考慮。3D struct 配列も網羅。内側 designator `[2]={[1]=...}` と内側 brace 内 `.member=` は global_struct_member_multidim_nested_designator で対応済み |
| **グローバル plain 多次元配列の `[N]={[M]=V}` designator** `int g[3][2]={[2]={[1]=99}}` | 🔧 | global_multidim_array_nested_designator_plain (続き73) | 続き13 (struct メンバ多次元 designator) と類似だが、struct メンバではなくトップレベル plain 多次元グローバル配列で発生する別経路。psx_gbrace_flat の外側 `[N]=` の elem_slots 計算が ctx.sub_dims を見るが、トップレベル ctx 初期化 (psx_parse_global_brace_init_flat) が sub_dims を埋めておらず、`[2]=` が単一スカラ scale で誤ジャンプ、99 が g[1][1] に書かれていた。修正: gv->outer_stride / mid_stride / extra_strides / deref_size の隣接ペアから sub_dims を算出して ctx に埋める。2D/3D/4D 配列・positional 混在・designator なし完全 positional の不変を網羅 |
| グローバルのネスト brace 配列添字 `{.items={[2]={.a=7}}}` | 🔧 | global_nested_brace_designator | flat 初期化パーサがネスト brace の再帰に「その level の集約型」コンテキストを渡さず、`.member` designator を常に最外 gv の tag で解決して E3064。型コンテキスト gbrace_ctx_t を再帰へ渡し ctx に対して解決するよう変更。併せて (a) ネスト配列 `[N]=` の絶対 slot に level 起点を加算、(b) 配列レベル positional 要素の境界整列 (部分初期化 `{.a=1}` 後のズレ) も修正。ネスト struct `.s={.a}`・配列添字・positional 混在・fp メンバ網羅 |

### struct / union ABI・値渡し
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| 小さい struct（3/5/6/7B）の値渡し/返し | 🔧 | 050e1bf | |
| <=8B struct を ternary / funccall から初期化 | 🔧 | 96b5510, 7697cf8 | |
| **ARM64 aggregate 値渡し/戻り/可変長引数** (c-testsuite 00204) | 🔧 | arm64_aggregate_varargs | 3/7/9B と HFA 系 aggregate の global 値引数・global struct return source・variadic aggregate を網羅。IR return/funcall の ND_GVAR aggregate アドレス化、`va_arg(ap, struct S)` の `*(struct S*)...` を同型 struct copy として扱う deref 初期化、`(struct S*)` cast の deref_size、長さ 1 配列メンバの array_len 保持と式中 decay、variadic aggregate の 8B stack slot 展開、`va_arg` の rounded sizeof advance を修正。副作用として length-1 配列 decay は scalar pointer member / funcptr array / pointer-to-array と区別する条件を追加 |
| static struct/union 局所の永続化 | 🔧 | static_local_struct_persist, 8167e8e | 続き228で inline anonymous tag の struct/union/array も file-scope tag へ昇格して global lowering 対象化 |
| long bitfield（>32bit / ユニット跨ぎ） | 🔧 | 040da11 | |
| bitfield の符号（signed sign-extend / unsigned）・幅切り詰め | ✅ | (probe u2, u6) | int:4/:1/:12/:20 等 |
| 非 bitfield メンバ直後の bitfield パッキング（`char c; int x:20`） | 🔧 | bitfield_pack_after_member | AAPCS: 収まれば同一 storage ユニットへ |
| pragma pack(1) と通常の混在 sizeof | ✅ | (probe bf3) | |
| ゼロ幅 bitfield `:0` のユニット分離 | ✅ | (probe bf1) | |
| enum 型 bitfield `enum E e:2` の符号 | 🔧 | bitfield_enum_and_static_init | tag-keyword 分岐で is_signed_type=1 のままで符号拡張し最上位ビット値が負に。非負列挙は unsigned 扱い (clang と同じ) |
| グローバル/静的 struct の bitfield 初期化子 | 🔧 | bitfield_enum_and_static_init | 同一 storage ユニットの先頭フィールドしか出力せず残り 0 (`{3,5}`→`.long 3/.long 5` で 0x53 でなく)。同一ユニットの bitfield を 1 整数に詰めて出力 |

| 整数リテラルの sizeof `sizeof(0)` | 🔧 | sizeof_int_literal | psx_node_type_size が ND_NUM に 0 を返し sizeof 既定 8 に落ちて `sizeof(0)`=8。ND_NUM を fp_kind/long サフィックスで判定 (int=4) |
| 文字列リテラルの sizeof がエスケープを生バイト数で数える / `sizeof(&array)` | 🔧 | sizeof_string_and_addr_of_array | `sizeof("\t")`=3 (raw 2+1)→デコード後長で。併せてグローバル `char g[]="a\tb"` の未デコード格納も修正。`sizeof(&arr)` は要素サイズ→`int(*)[N]` なので 8 |

### プリプロセッサ
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| `#if` 定数式のビット/シフト/剰余/三項 (`& | ^ << >> % ?:`) | 🔧 | pp_if_operators | 定数式パーサにレベル欠落で `#if (3&5)==1` 等が E1006。conditional/bitor/bitxor/bitand/shift と mul の % を追加 |
| `#if` 定数式の `&&`/`||`/`?:` 短絡評価 (未選択側のゼロ除算 skip) | 🔧 | pp_if_short_circuit | `#if 0 != (0 && (0/0))` 等で右辺が評価され E1006。logand/logor/conditional で短絡 + `||` 短絡時は結果 1 に正規化。`&&` 右辺は bitor レベル (優先順位修正)。c-testsuite 00145 |
| `#line` 指令の引数マクロ展開 | 🔧 | pp_line_macro_arg | `#line line` (`#define line 1000`) が raw TK_IDENT のまま無視され __LINE__ が更新されない。c-testsuite 00152 |
| 定義済みマクロ `__LP64__` (ARM64 LP64) | 🔧 | pp_predefined_lp64 | Apple ARM64 ターゲットなのに未定義で c-testsuite 00212 が失敗 |
| トップレベル宣言の関数プロトタイプと変数の混在 | 🔧 | mixed_decl_func_proto_and_var | `int f(int), g(int), a;` が funcdef 経路になり E2006。c-testsuite 00121 |
| pointer-to-function (戻り funcptr) の呼び出し / 連鎖呼び出し | 🔧 | func_returning_funcptr_call, func_returning_funcptr_chain | `(*p)(a,b)` と `(*(*p)(a,b))(c,d)` が二重ロードで SIGBUS (00124)。`go()()->zerofunc()` は 2 段目 funcall の戻り tag 未伝播で E3005 (00089)。typedef/関数の funcptr 戻り型と指し示す関数の戻り tag を記録し、間接 funcall の callee が funcptr 返し funcall のとき導出する |
| グローバル多次元配列の最外 `[]` 次元推論 + 部分行初期化 | 🔧 | global_incomplete_outer_array_dim | `int arr[][3][5]={...}` で行境界ずれ + 外側次元誤推論。c-testsuite 00151 |
| 可変長マクロの空 __VA_ARGS__ `F(a,...)`→`F(42)` | 🔧 | variadic_macro_empty_va | コール側引数チェック `parsed_args <= num_named` が空 va を E1024 で拒否。`< num_named` に緩め名前付き不足時のみエラー (clang/gcc 互換) |
| 文字列リテラルと stringize 結果の連結 `"a" S(b)` | 🔧 | string_concat_stringize | stringize 結果は char_width=0。parse_string_literal_sequence の幅比較が 0 を正規化せず 2番目以降で E3002。比較側も 0→CHAR 正規化 |
| `#` stringize が文字列リテラル引数のクォート/エスケープを落とす | 🔧 | stringize_string_literal | `STR("hi")` が `"hi"` でなく hi に。C11 6.10.3.2 通り囲み `"` を保持し内部の `"`/`\` の前に `\` を挿入 (token_text は引用符なし内容を返すため再構築) |
| 空のマクロ実引数 `F(7,)` / `F(,8)` / `F(a,,c)` | 🔧 | empty_macro_argument | C99 6.10.3p4 で空引数は合法 (placemarker) なのに has_empty_arg で E1024 拒否。引数個数チェックは残し空引数自体ではエラーにしない |
| **二段 paste + 直後呼び出し** `CAT(A,B)(x)` (`#define CAT(a,b) CAT2(a,b)` / `AB(x) CAT(x,y)`) | 🔧 | macro_nested_paste_call | ストリーム経路で `)(` 後続が置換列と分離し `AB (x)` 化。さらに hideset を親鎖ごと伝播すると paste 済み AB からの `CAT(x,y)` が外側 CAT でブロック。`)(` サフィックスを pull コピーして preprocess_ctx で縮約 + hideset は展開マクロ名のみ付与。c-testsuite 00201 |
| **stream macro 展開下の cast 先読み窓** `PTYPE((X) << (unsigned long)1)` | 🔧 | shift_left_operand_type | c-testsuite 00200 の長大マクロで `(int) sizeof(...)` の `sizeof` が未 materialize のまま parse_cast_type が `(int)` だけを cast と誤認し SIGSEGV。cast 型先読み前に tk_ensure_lookahead() を呼び、`)` 後 NULL は成功扱いしない。併せて `CAT(A,B)(x)` 用の `)(` splice は単一関数風呼び出し置換列 (`CAT2(A,B)`) に限定し、PTYPE のような任意式マクロで外側 stream を余分に覗かない |
| **空引数と `##` の placemarker** `P(jim,) => A##B ; bob` / `Q(+,)3` | 🔧 | macro_paste_empty_operand | 空引数を何も挿入せず `jim ## ;` と並べ E1030 になっていた。C11 6.10.3.2p3 通り placemarker 側の `##` を削除し非空側のみ残す。c-testsuite 00202 |
| **未完了タグのポインタ / 関数型仮引数** `enum E *e` / `enum E const *e2` / `int f(int (int x), int)` | 🔧 | incomplete_tag_and_nested_func_param | 未完了 enum/struct を前方宣言せず E3066。タグ直後の const を読まず E3016。`(int x)` を nested param list として解釈せず E2006。c-testsuite 00209 |
| **GNU `__attribute__` の読み飛ばし** `} __attribute__((packed)) T` / `int ATTR f()` / cast 内 `ATTR` | 🔧 | gnu_attribute_parse | `__attribute__((...))` を未対応で E2006。宣言・関数・キャスト各所で括弧グループごと skip するのみ (属性意味は未実装)。c-testsuite 00210 |
| **GNU statement expression** `({ ...; expr })` / 三項偽側 | 🔧 | gnu_statement_expression | `({` を primary で compound literal と区別せず E3064。ND_STMT_EXPR でブロック実行後に最終式の値を返す。c-testsuite 00213 |
| **`__builtin_expect(exp, c)`** 分岐ヒント builtin | 🔧 | builtin_expect_fold | 未宣言関数として ND_FUNCALL 化し `_builtin_expect` 未定義シンボルで link fail。GCC 同様第1引数 exp のみ評価して返す (hint 無視)。c-testsuite 00214 |
| `#if 0` 内の非 C トークン (`@` `$`・未終端リテラル・不正数値) の skip | 🔧 | if0_skip_non_c_tokens | 全体 tokenize 後に preprocess する構造のため、偽分岐内の未知文字・未終端リテラル (`don't` の `'`)・不正数値が tokenize 時に E2028/E2018 で先に落ちていた。寛容モードを導入: プリプロセッサが pps_skip_cond_incl と pps_materialize_line の行末先読みの間だけ tk_set_tolerate_untokenizable(true) を立て、tk_stream_next が setjmp を張る。scanner の各種 TK_DIAG_* は寛容モード中のみ tk_tolerate_longjmp_if_active 経由で longjmp し、当該トークンを 1 文字の TK_UNKNOWN にして進める (skip が捨てる)。over-pull で active に漏れた TK_UNKNOWN は pps_step が E2028。active コードの不正は従来どおり即エラー (位置も正確) |

### 型機能 / その他
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| `_Alignof` の集約アラインメント | 🔧 | 3e8a4d1 | |
| `sizeof`/`_Alignof` の複数語整数型 `(long long)` 等 | 🔧 | sizeof_multiword_int | parse_parenthesized_type_size が先頭1語のみ消費し `sizeof(long long)` が E2006。整数型指定子列を一括解釈 |
| `_Generic` の文字列/long リテラル/ポインタ種別 | 🔧 | e0b5190 ほか | |
| `_Generic` のスカラ整数キャスト制御式 `(char)x` | 🔧 | generic_scalar_cast_control | `(T)0` idiom。ポインタ/関数/タグは infer 維持 |
| `_Generic` の修飾子除去 / ポインタ段数 | ✅ | (probe gen1) | |
| `_Generic` 深いネスト型 (`(int(*(*)(void))[3])`) の同一型マッチ | 🔧 | generic_complex_derived_type{,_global} | 構造的フィールドでは区別不能だったため、型を正規化トークン文字列にして照合 (control が局所変数 / キャスト / グローバル変数のとき有効)。control/assoc 双方に sig があれば strcmp、片方のみは従来照合 (加算的) |
| `_Generic` が `long` と `long long` を同一視 | 🔧 | generic_long_vs_longlong | リテラル `0LL`/`0ULL` の LL サフィックス (token int_size) を node→generic_type へ伝播し、同サイズ整数でも long long rank を照合 |
| `_Generic` が `char`/`signed char`、`long`/`long long` を**変数**で同一視 | 🔧 | generic_char_and_longlong_identity | 制御式が変数だとサイズと符号しか持たず誤マッチ。is_long_long / is_plain_char の型識別を宣言 (psx_consume_type_kind の side-channel)→lvar→参照ノード→infer_generic_control_type へ伝播し generic_type_matches で照合。assoc 側は parse_integer_cast_spec_sequence が is_plain_char を出力 |
| `_Generic` が int 制御式を単一 int メンバの struct に誤マッチ | 🔧 | generic_struct_vs_scalar | generic_type_matches が control 側しか tag を見ず、`_Generic((int), Anon:.., int:..)` がサイズ一致でスカラ経路で Anon に化けた。どちらか一方でも tag を持てば tag 一致を要求 |
| **`_Generic` の配列 association と関数 designator** `int[4]` / `typedef int (*F)(int); _Generic(foo, F:...)` | 🔧 | generic_array_assoc_and_func_designator | c-testsuite 00219。関連型 `int[4]` の配列 suffix を読み飛ばすだけで配列型の印を残さず、scalar `int` とサイズ一致で誤マッチしていたため `is_array` を追加。関数名制御式 `foo` は ND_FUNCREF のまま戻り値 `int` のように扱われ、function pointer typedef に一致せず `int:` 側へ落ちていたため、ND_FUNCREF を function pointer 型として `_Generic` に渡す。直接書いた複雑 function pointer association は既存の type_sig 照合を優先し、関数名側に sig が無い場合は broad match しない。 |
| `_Generic` が引数違いの関数ポインタを同一視 | 🔧 | generic_complex_derived_type{,_global} | `int(*)(int)` と `int(*)(int,int)`、`int(*)(int)` と `double(*)(int)` を区別。型を正規化トークン文字列化して比較するので引数の個数・型・戻り型まで照合する。制御式が局所変数 / キャスト `(T)0` / グローバル変数のいずれでも有効 |
| `sizeof`/`_Alignof(enum E)` | 🔧 | sizeof_enum_type | parse_parenthesized_type_size が struct/union/typedef のみ対応し enum 型名を E3064 で拒否。enum は int 相当 4B として TK_ENUM 分岐を追加 (sizeof(enum 変数) は元から動作) |
| **`static` ローカル配列の `sizeof`** `static int a[10]` | 🔧 | static_local_array_sizeof | sizeof が要素サイズ(4)を返していた。static ローカル配列は try_lower_static_local_array でグローバルへ lowering され alias lvar が is_array=0/size=0 で登録されるため、parse_sizeof_operand の `arr_var->is_array` 特別処理を素通りし一般経路 ND_ADDR(ND_GVAR) の type_size=要素ストライド(4) になっていた。lvar_is_static_local_array のとき lowering 先グローバルの type_size (=全体サイズ) を返す分岐を追加。int/char/double・`sizeof(a)/sizeof(a[0])` イディオム |
| **多次元 `static` ローカル配列の永続化** `static int a[2][3]` / `static double d[2][2]` / `static int *p[2][2]` / `typedef int M[2][3]; static M m` / `typedef int *P[2][2]; static P p` | 🔧 | static_local_multidim_array, static_local_pointer_array_init, static_local_typedef_multidim_array | 1D static local 配列は lowering されるが、多次元 suffix は `try_lower_static_local_array` が `[` の連続を見て対象外にし、通常 auto 多次元配列として frame に置かれていた。呼び出し間で値が保持されず、Wasm IR でも alloca になっていた。多次元定数 suffix を peek/consume して consumed lowering へ渡し、global/alias に outer/mid/extra stride を保存、`ND_ADDR(ND_GVAR)` へ伝播。続き229で static local alias の unsigned pointee 伝播漏れと FP 配列の `fp_kind` / `init_fvalues` 漏れを修正し、unsigned char/short/long/double も網羅。続き230で typedef-array local 登録より前に static typedef-array lowering を追加し、`static M m` も stack local に落ちないようにした。続き231で pointer-element typedef 多次元配列の leaf 要素サイズを row size から復元し、static alias の `pointer_qual_levels/base_deref_size` を `ND_ADDR(ND_GVAR)` へ伝播するよう修正。続き232で直書き `static int *p[2][2]` の `[` suffix 経路も pointer-element static lowering に入れた |
| `_Alignas(>16)` のローカル変数 | 🔧 | alignas_overaligned_local | x29 が 16 整列のみで固定オフセットでは過剰整列不可。align_bytes>16 のローカルだけ予備領域+実行時丸め (IR_ALIGN_PTR=add #A-1;and #-A)。併せて `_Alignas(N) struct ...` 局所の (N) を読み飛ばせず E3015 だったパースも修正 |
| enum 値・算術 | ✅ | (probe q3) | |
| switch / fallthrough / default | ✅ | (probe p4) | |
| 複合代入演算子 `<<=` `>>=` `&=` `|=` `^=` `%=`（型幅・符号） | ✅ | (probe ca1, ca2) | sub-int/メンバ/ポインタ deref/配列要素 |
| do-while / goto / 多重代入 / 前後置インクリメント | ✅ | (probe q4, q7) | |
| **typedef 名と同名ラベル** `goto s;` … `s:` (typedef `s` と shadow) | 🔧 | typedef_label_shadow | `s:` が is_decl_like_start_stmt (typedef 名) より先に宣言として解釈され E3064。`ident:` ラベル形を宣言判定より優先。c-testsuite 00129 |
| **明示 `extern` 付き関数内 prototype** `extern int f(int); f(1)` | 🔧 | local_function_prototype | 続き77 で `int f(char *);` は暗黙 extern の関数 prototype としてローカル変数登録を避けるよう修正済みだったが、`extern` 付きは parse_local_extern_declarator_list の別経路で `global_var_t` の extern 変数として登録され、呼び出しが直接 `bl _f` ではなく GOT load した値への間接 call になって SIGBUS。extern 宣言子ループでも g_decl_trailing_func_suffix をリセット/検出し、non-pointer の関数 declarator は extern 変数登録せず読み飛ばす。`extern int f(int), value;` の混在も網羅 |
| **関数内 `extern struct/union T obj;`** `extern struct S gs; gs.x` | 🔧 | local_extern_tag_decl | stmt.c の tag-keyword fast path が `extern` を消費しても storage class を復元せず、`struct S gs;` と同じ auto 変数として登録して未初期化スタックを読んでいた。tag 経路で extern を保存/復元し、after_type_ex 側でも extern なら local extern 登録へ回す。登録時に tag/fp/unsigned 情報を保持し、先行定義・後方定義・union・struct pointer を網羅 |
| compound literal（引数・式中） | ✅ | (probe q6) | |
| ファイルスコープ複合リテラル初期化子 `T g=(T){...};` | 🔧 | file_scope_compound_literal_init | struct で `,` で E2006、scalar で先行関数宣言があると x=0。`T g=(T){...}` は `T g={...}` と等価 (C11 6.5.2.5) なので先頭の `(型)` を読み飛ばし既存 brace 初期化へ。scalar/array/struct 対応 |
| ファイルスコープ `static struct/union/enum var` | 🔧 | static_tag_global | parse_toplevel_decl_spec が tag 判定前に storage class を skip せず、`static struct S g` を E3016 で拒否。tag 前に修飾子先読み skip を追加 |
| **storage class 付きタグ戻り関数** `static struct S *f(){...}` | 🔧 | static_tag_return_function | is_toplevel_function_signature と parse_func_decl_spec がどちらも「タグキーワードの前の storage class (static/extern)」を飛ばさず、`static struct S *g()` がオブジェクト宣言と誤判定 (E2006 `;` 期待) / 戻り型が implicit int に化け E3064。`static int *g()`(builtin) や非 static `struct S *g()` は動作。両関数で storage class の直後がタグキーワードなら storage class を消費してタグ経路へ。struct/union/enum・ポインタ/値返し・引数あり・戻り値 subscript を網羅 |
| **ファイルスコープ `static <typedef名> 変数`** `static Point p;` | 🔧 | static_typedef_name_global | parse_toplevel_decl_spec が storage class (static/extern) を tag キーワードの前でしか skip せず、typedef 名の前では skip しなかったため `static Point p` で `static` が残り Point が型と認識されず E2006 (`;` 期待)。非 static や builtin (`static int`) や tag (`static struct S`) は動作。typedef 名の前にも修飾子先読み skip を効かせる (toplevel_prefix_precedes_typedef_name)。const 修飾・ポインタ・配列・named/anon typedef・関数戻りを網羅 |
| **const/volatile 付きポインタ戻り型** `int *const f()` | 🔧 | qualified_pointer_return | is_toplevel_function_signature の先読みと parse_pointer_suffix_flags が `*` の後の const/volatile を飛ばさず、関数と認識されずオブジェクト宣言と誤判定して E2006 (`;` 期待)。`const int *f()` (pointer to const) は動作。lookahead (3 関数の `*` skip ループ) と suffix 解析の両方で `*` の後の const/volatile を読み飛ばす。int/double/struct・const/volatile・subscript 併用を網羅 |
| **const pointee の関数戻り値経由メンバ代入** `const struct S *f(); f()->x=...` / `const struct S (*f())[N]; (*f())[i].x=...` | 🔧 | const_func_ret_pointee_member_assign (続き105) | `const struct S *get(void)` が parse_func_decl_spec の tag fast path に入らず implicit int に落ちていた。さらに関数戻り値の pointee const/volatile を semantic ctx に保存していなかったため、直接呼び出し結果の `->` / `*` / `[]` 経由の lvalue に const が伝播しなかった。tag 前の const/volatile を consume し、関数定義/プロトタイプ登録で ret_pointee qualifier を保存、build_member_deref_node / build_unary_deref_node / build_subscript_deref が ND_FUNCALL から qualifier を復元する。通常 pointer 戻りと pointer-to-array 戻り、読み取り OK / 書き込み E3077 を網羅 |
| **const pointee の関数ポインタ戻り値経由メンバ代入** `const struct S *(*fp)(); fp()->x=...` / `const struct S (*(*fp)())[N]; (*fp())[i].x=...` | 🔧 | funcptr_return_const_pointee / compile_fail (続き106) | 続き105 の間接呼び出し版。`ND_FUNCALL.callee != NULL` では関数名表を引けず、callee の関数ポインタ型にある pointee const/volatile を見ていなかったため `fp()->x=...` が通っていた。また pointer-to-array 戻りでは `*fp()` の結果に戻り tag/要素サイズが伝播せず、合法な `(*fp())[0].x` 読み取りも E3005。修正: 間接呼び出しは callee の `node_mem_t` から pointee qualifier を復元し、`build_unary_deref_node` の ND_FUNCALL indirect 経路で callee の戻り tag と要素サイズを carry。読み取り・mutable 書き込み OK、const 書き込み E3077 を網羅 |
| **多段ポインタを返す関数の直接 deref** `int **g(); **g()` | 🔧 | multilevel_pointer_return | semantic ctx の ret_is_pointer が bool で段数を持たず `int **` を単段 `int *`(pointee 4B) 扱い→`*g()` が int になり `**g()` が int 値をアドレス参照で SIGSEGV。型付き変数経由 `int **q=g(); **q` は元から動作。戻り型の `*` 段数を ret_pointer_levels に記録し、node_utils の funcall 経路 (pointer_qual_levels / base_deref_size / ps_node_deref_size) が段数>=2 のとき `*g()` を「1 段減らしたポインタ」(8B 値・最内基底型 deref) として組む。build_subscript_deref も funcall base を 1 段消費 (`g()[i]`→int*)。int**/char**/int***・prefix deref・deref+subscript 混在 `(*rg())[1]`・直接 subscript `rg()[0][i]` を網羅。単段ポインタ戻りは不変 (段数>=2 ゲート)。`int *const *f()` 等の qualified 多段もこれで解禁 |
| **タグ戻り + `(*...)` 宣言子** `struct P (*f())[3]` / `struct R (*f())(int)` | 🔧 | tag_return_complex_declarator | is_tag_return_function_signature が `(*...)` 宣言子を扱わず (`(` 後に IDENT を期待し `*` で return 0)、変数宣言と誤判定して E2006。builtin `int (*f())[3]` は is_toplevel_function_signature が処理できていた。両関数の宣言子判定を共有ヘルパ is_function_declarator_sig に抽出し、タグ戻りでも配列へのポインタ戻り・関数ポインタ戻りを検出 |
| **struct を返す関数ポインタの間接呼び出しメンバアクセス** `op(41).v` / `op(41)->v` | 🔧 | funcptr_return_struct_member | 間接呼び出し (callee != NULL) の funcall ノードに戻り tag 型が伝播せず psx_node_get_tag_type が TK_EOF を返し E3005 (`.`/`->` の左辺が構造体でない)。直接呼び出し `mk(41).v` は ret_tag 表から引けて動作。callee の funcptr 変数は tag フィールドに戻り tag を保持するので導出し、戻り値がポインタか否かは pointer_qual_levels で判定 (値戻り `struct R (*op)()`=pql1→ptr0 / ポインタ戻り `struct R *(*op)()`=pql2→ptr1)。値戻り/ポインタ戻り・deref形 `(*op)(x).v`・global funcptr・union 戻り・8B ネストメンバ連鎖を網羅。1/2/4/8B のレジスタ返し限定 (非 pow2 サイズは下行で対応) |
| **1/2/4/8B 以外の struct を返す関数ポインタの間接呼び出し** `struct Big (*ob)(int); ob(100).a` | 🔧 | funcptr_return_large_struct | x8 ret_area 間接返し ABI を direct call 限定で実装しており、間接呼び出しは IR build 失敗 ("ir build/emit failed")。メンバアクセス以前に `struct Big r=ob(100);` 単独でも落ちていた。直接呼び出し `mkbig(100).a` は動作。3 箇所を修正: (1) parse_call_postfix が間接 funcall ノードに ret_struct_size を未設定 (0) → callee funcptr の戻り tag (pql<=1 値戻り) からサイズ導出。(2) build_assign_struct が間接 struct 戻りを明示 fail → 汎用 funcall 経路へ委譲し ret_area から dst へ memcpy。(3) build_node_funcall の ret_area 確保が `!fn->callee` 限定 → direct/indirect 両方で確保 (codegen は x8 設定と blr を独立に出す)。12B/16B/20B struct・16B union・変数代入・直接メンバ・deref形・global funcptr・間接戻り値の値引数渡しを網羅。Wasm backend でも `call_indirect` の hidden ret_area `i32` 先頭 param を出し、local/global/struct member funcptr の 12B struct 値返しを WAT + 実行値で網羅 |
| **Wasm 制御フロー越し関数ポインタ呼び出し** `if(1) g=set7; g(&x);` / `ops.f(&x)` | 🔧 | wasm32_*_funcptr_control_flow_store, wasm_nonvoid_indirect_unused_result (続き116/239/240) | global/struct member の関数ポインタが制御フロー越しに上書きされると、Wasm emitter が callee 名を逆引きできず、void call と非 void unused-result call を区別できず E4008。関数ポインタ型に `funcptr_ret_is_void` を保存し、typedef/local/global/tag member/`node_mem_t` から `ND_FUNCALL.is_void_call` と `IR_CALL.is_void_call` へ伝播。続き239で非 void unknown indirect の未使用結果も `IR_CALL.dst.type` から typeuse を組み、WAT は `drop (call_indirect ... (result T))` として出せるようにした。続き240で Wasm object も objdump と link/run で確認 |
| **Wasm E2E subset / mixed int+fp 直接関数引数** `int f(int,double,int)` | 🔧 | test_wasm32_e2e / double_param_int_param_mix (続き117-121/241-244) | native `test_e2e` は Wasm backend を通らないため、既存 assert fixture を Wasm 用に変換し `ag_c_wasm -> wat2wasm -> wasm-validate -> wasm-interp` で `main() => i32:0` を確認する `test/test_wasm32_e2e.c` を追加し `make test` に組み込み。現時点で 1003 件を実行。続き118で arithmetic/switch/array/type_decl/inline/flex array/pragma pack/evil/func_name/string/stdheader 定数系/struct by-value/struct return まで拡張し、続き119で preflight 済み 447 件を `test/wasm32_e2e_extra_cases.txt` として追加。続き120で pointer operand を含む整数 binop を Wasm `i32` 演算に正規化し、比較結果型を `i32` として扱い、`i32.const` を 32bit 表現にすることで `pointer/array_decay_diff.c` など追加 19 件を回収。さらに byref struct 仮引数の Wasm signature を `i32` に合わせ、indirect callee vreg を table index (`i32`) として型付けして large struct by-value/direct member funcptr 系 4 件を追加回収。続き121で関数ポインタ型の戻り pointer/整数幅を indirect call の `IR_CALL.dst.type` へ伝播し、long 戻りと関数ポインタ返しチェーン 2 件を追加。導入時に `IR_PARAM` の `src1.imm` が integer/fp 別 ABI index なのに、Wasm signature 生成が単一 param list index として扱い、`int,double,int` が `(param i64 i64)` に潰れて WAT type mismatch。Wasm signature と entry `local.get $pN` は IR_PARAM 出現順で並べる。続き241で output-only minimal libc stub に `puts`、続き242で raw `assert.h` 用の `__assert_rtn` stub を追加。続き243で WAT c-testsuite preflight を 206/218 から 214/218 へ改善し、`strlen` 旧宣言、empty indirect table、string/calloc/sprintf minimal stub を backend test で網羅。続き244で forward 関数 table refs、`fprintf` 関数ポインタ stub、`getc` 宣言、file I/O minimal stub を追加し、WAT c-testsuite preflight は 218/218 compile + validate green |
| **ポインタ typedef 仮引数の subscript** `typedef char* Str; len(Str s){ s[i] }` | 🔧 | pointer_typedef_param_subscript | param_decl_spec_t が typedef のポインタ性 (_ti.is_pointer) を捕捉せず、宣言子に `*` が無い (param_is_ptr=0) ため register_param_lvar のポインタ分岐に入らずスカラ登録され `s[i]` が E3064。deref `*s` は build_unary_deref が 8B 値を寛容に許し動作、直書き `const char* s` も動作。typedef 基底のポインタ段数を捕捉し宣言子の `*` と合成して実効ポインタ性を決める。char*/int*/const char*/struct*・多段 typedef・typedef+宣言子 `*`・ポインタ算術・添字代入を網羅 |
| **unsigned char/short ポインタ経由の zero-extend** `unsigned char* p; p[i]` | 🔧 | unsigned_char_pointer_zero_extend | pointee が符号拡張 (ldrsb/ldrsh) され 200→-56 等に化けた。unsigned スカラ・unsigned 配列要素は元から正常。3 経路を修正: (1) local subscript の最終要素判定 `pql==0 && inner_ds==0` が単段ポインタ (pql=1/inner_ds=elem) を最終要素と認識できず → fp の中間行判定と対称な `!is_pointer && !(inner_ds>0 && es>inner_ds)` に。(2) 仮引数 `unsigned char* p` の pointee unsigned を param_decl_spec_t に捕捉し var->is_unsigned へ伝播。(3) `*(p+i)` の ND_ADD/SUB operand を辿る node_pointee_is_unsigned ヘルパ追加。signed char*/short* は符号拡張維持。local/param・deref/subscript/arith・unsigned short・多段/2D を網羅 |
| **グローバルの 2 次元以上のポインタ配列** `int *t[2][2]` / `char *names[2][2]` / `int(*t[2][2])(void)` | 🔧 | global_2d_pointer_array | `t[i][j]` が SIGSEGV (非ポインタ `int t[2][2]` は動作)。3 修正: (1) apply_global_multidim_strides の `!head.is_ptr` ゲートでポインタ要素配列を除外し stride が立たず `t[i]` を「ポインタ値 load→[j] で deref」と誤計算→ゲートを外し elem_size=8 で stride。(2) build_subscript_deref の pointee_is_scalar_ptr が中間次元でも load していた→最終次元 (inner_ds==0) のみ load し中間は伝播 + 要素 pointee サイズを base_deref_size で carry (最終 base は中間 ND_DEREF で gv を引けないため)。(3) 括弧内配列 `(*t[2][2])` の paren_array_mul は積(4)のみで dims を捨てていた→psx_parse_array_suffixes_capture_dims で {2,2} を捕捉。2D/3D データ・char* 文字列・要素代入・2D funcptr (call/値) を網羅 |
| **ローカルの 2 次元以上のデータポインタ配列** `int *t[2][2]` | 🔧 | local_2d_pointer_array | `*t[i][j]` が SIGSEGV (グローバル版は別行、非ポインタ `int t[2][2]` は動作)。register_multidim_array_lvar が outer_stride を立てるが登録後に pql=1/base_deref_size=4 を立てるため、build_subscript_deref の「要素はポインタ」分岐 (pql>=1 && bds>0) が **1 段目** `t[i]` で発火し deref_size を inner_ds(8) から bds(4) に上書き→2 段目が +4/ldrsw (4B) に化けた。fp/unsigned と同じ中間行判定 (inner_ds>0 && es>inner_ds) で 1 段目を中間行と認識し pointer-element 化を最終次元まで遅延・pql/bds を carry。2D/3D・char*・代入を網羅。1D `int *arr[N]`・genuine `int **pp` は不変 |
| **ローカルの 2 次元以上の関数ポインタ配列** `int(*t[2][2])(void)` | 🔧 | local_2d_funcptr_array | ネスト brace init `{{a,b},{c,d}}` が E3064、個別代入でも `t[i][j]()` が SIGSEGV (1D `int(*ops[N])(int)` は動作)。funcptr 配列の局所登録 (decl.c:3185) が括弧内 `[N][M]` を inner_array_mul の積に潰し flat 1D 登録で多次元 stride 未設定。括弧内個別次元 (g_inner_array_dims) を捕捉し outer_stride/mid_stride (要素 8B funcptr) を設定。stride が立つことで 2D 配列と正しく認識され brace init も通る (E3064 も解消)。2D/3D・brace init・個別代入・値→呼び出し・引数つきを網羅。1D は不変。これで 2D ポインタ配列 (global/local × data/funcptr) を全て解消 |
| **UTF-16/UTF-32/wide 文字列リテラルの配列初期化** `unsigned short s[]=u"hi"` | 🔧 | wide_string_literal_init | 文字定数 `u'A'` は動くのに配列初期化子で壊れた: 明示サイズは値が 0、`[]` は E3064、global は .comm 0。ローカル init / `[]` 推論 / global init の 3 経路が要素幅 1 (char) 決め打ち。要素幅が char_width (char/u8=1,u=2,U/L=4) と一致するとき各コード単位を要素幅で格納し type_size=count*elem。ASCII 内容のみ |
| **非 ASCII の UTF-16/UTF-32 文字列リテラル** `U"aあb"` | 🔧 | wide_string_literal_init | 非 ASCII を UTF-8 バイトのまま code unit 化し `U"aあb"` が 97,227,129,130,98 (6) に。emit / 配列 init(local/global) / 要素数推論の 4 箇所がバイト単位。tk_decode_utf8 + 幅対応 tk_next_string_code_units を追加し全箇所を統一 (char/u8=1byte、u=UTF-16 BMP1/補助面サロゲート対、U/L=UTF-32 1/code point)。BMP・サロゲートを網羅 |
| **グローバル struct の char 配列メンバの文字列初期化** `struct S{char name[8];} g={"main"}` | 🔧 | global_struct_char_array_member | char[] メンバを char* と取り違え、文字列ラベルのアドレス (.quad .LC0) を 8 バイトに格納し name 全体がポインタ値に化けていた。global brace flat パーサ (psx_gbrace_flat) が char 配列メンバ (tag 無し・要素 1 バイト・array_len>0) の文字列をポインタでなく array_len バイトへ展開するよう修正 (多次元 char 配列の行展開と同じ機構)。要素サイズは tag_member_info.type_size から (char 配列メンバは deref_size=0)。scalar/配列メンバ併存・escape・短い文字列 (0 埋め)・ローカル struct を網羅。char[] メンバの後に char* メンバが続く形 (旧「未対応 (a)」) は別バグ (下記 fp_kind 汚染) が真因で global_struct_member_after_fp_decl で修正済み。2D/3D 以上と brace elision は global_struct_2d_char_array_member / local_struct_2d_char_array_member / local_struct_3d_char_array_member / multidim_char_member_brace_elision で対応済み。struct 配列内の char メンバ (c) は global_struct_array_char_member で修正済み。**大きめプログラムの差分探索で発見** |
| **グローバル struct 配列要素の char 配列メンバ** `struct{char tag[4]; int n;} g[2]={{"aa",1},{"bb",2}}` | 🔧 | global_struct_array_char_member | emit_global_struct_array_init がメンバごとにフラット slot を 1 個だけ消費する単純ループで、配列メンバ (`char tag[4]`)・char 配列の文字列展開・入れ子 struct メンバ・bitfield を扱えず、tag が 1 バイトしか出ず (`.byte 97; .space 3`) 後続メンバ n に 2 文字目 'a'(97) が入り総崩れ。emit_global_struct_array_init を各要素について emit_global_struct_members_rec を呼ぶ形に書き換え、非配列 struct (emit_global_struct_init) と同じメンバ展開機構を要素ごとに適用して統一 (配列メンバ/char 配列展開/入れ子 struct/bitfield/部分初期化ゼロ埋め共通処理)。parser 側は元から各要素の char 配列をバイト展開できており emit 側のみの不具合。char配列先頭+スカラ・スカラ先頭+char配列・部分初期化・入れ子struct要素・char配列2本を網羅。**HANDOFF サブケース (c)。差分テストで発見** |
| **グローバル struct の 2 次元 char 配列メンバの文字列初期化** `struct{char rows[2][4];} g={{"ab","cd"}}` | 🔧 | global_struct_2d_char_array_member | 2 次元 char メンバの行幅 (outer_stride=4) が global brace flat パーサの gbrace_ctx_t に伝わらず、ネスト brace `{"ab","cd"}` の各行文字列が要素 (char) 扱いされ array_len=0 でポインタ (.LC ラベル) 出力 (`.quad .LC0; .quad .LC1`) になり行データがポインタ値に化けていた。gbrace_ctx_t に row_width を追加し多次元 char メンバ (tag 無し・elem 1B・outer_stride>0・array_len>outer_stride) で outer_stride を行幅に持たせ、gbrace_child_at が各要素を内側 1 次元 char 配列 (char[row_width]) として返し既存 char 配列展開分岐 (8c8ce2a) に乗せる。2D 基本/後続スカラ/先行スカラ/短い文字列 0 埋めを網羅。3D 以上は local_struct_3d_char_array_member、brace elision は multidim_char_member_brace_elision、ローカル 2D は local_struct_2d_char_array_member で対応済み。**HANDOFF サブケース (b)。差分テストで発見** |
| **fp 宣言の直後に来る tag グローバルの decl-spec fp_kind 汚染** `typedef long double T; struct{char b[4];char*p;} g={"x","y"}` で p が `.quad 0` | 🔧 | global_struct_member_after_fp_decl | トップレベル dispatcher (ps_next_function) が tag キーワード始まりの宣言を parse_toplevel_tag_decl へ直接回す経路で reset_toplevel_decl_spec_state を呼ばず、g_toplevel_decl_fp_kind が前宣言 (例: stddef.h の `typedef long double max_align_t;`→string.h 等が間接 include) の DOUBLE のまま残り、struct/union object の fp_kind が DOUBLE になっていた。すると global brace init の fp-fold 経路 (gv->fp_kind != NONE) が文字列リテラル/関数参照/アドレス初期化子を fp 定数(0)として食べ後続メンバが NULL/0 に化けた。parse_toplevel_tag_decl 冒頭の手動 extern/static リセットを reset_toplevel_decl_spec_state() に置換し宣言ごとに全クリア (tag 情報は install_toplevel_tag_decl_globals が再設定)。char[]+char*・funcref 初期化子・アドレス初期化子・文字列ポインタを網羅。**HANDOFF サブケース (a) の真因。先行 fp 宣言が無ければ char[]+char* 自体は元から動作。差分テストで発見** |
| **_Generic で long double を double と区別** `_Generic((long double)x, long double:.., double:..)` | 🔧 | generic_long_double | ag_c は long double→double lowering (fp_kind=DOUBLE) のため long double 制御式が `double:`/`default:` に当たっていた。long long/plain char と同じ side-channel ビット (node_mem_t.is_long_double) を宣言時に立てノードへ伝播し infer_generic_control_type が読む。fp_kind は不変で値は double と同一 (Apple ARM64)、_Generic 選択のみ変わる。ローカル変数・cast に加え、global/param と `typedef long double LD;` / `typedef LD LD2;` 経由の local/global/param へも typedef/global/param 型メタデータから伝播する。マクロ経由で制御式が `((T)x)` になる cast も parse_generic_selection で cast 型を採用する。 |
| **C11 標準ヘッダ 10 個の同梱** iso646/stdalign/stdnoreturn/uchar/inttypes/fenv/locale/wctype/wchar/tgmath | 🔧 | c11_standard_headers | 欠落していた C11 ヘッダを追加 (関数実体はシステム libc)。tgmath 対応で 4 件のコンパイラバグも修正: (1) `long double` が _Generic 関連型でパース不可 (整数 cast-spec が `long` だけ食べる)→次が `double` なら整数列でないと判断。(2) `double` 制御式が `long double:` に誤マッチ (共に kind=TK_DOUBLE)→is_long_double で区別。(3) 外部関数アドレス `fp=sqrt` が adrp @PAGE 直参照でリンク失敗→GOT 経由 (@GOTPAGE)。(4) `_Generic(...)(args)`/`(name)(args)` の bare funcref 呼び出しが間接化しシグネチャを失い fp 戻り値を x0 で読む→funcref callee を直接呼び出しに変換しプロトタイプ ABI を適用。ASCII のみ |
| `void *` 戻り型を void 関数と誤判定 | 🔧 | void_ptr_return | (整数型の節参照) return チェックが is_pointer 無視 |
| 間接呼び出しの int→fp 引数昇格 (funcptr / typedef funcptr / struct member funcptr) | 🔧 | funcptr_int_to_fp_arg, typedef_funcptr_int_to_fp_arg, funcptr_member_int_to_fp_arg | (関数ポインタ節参照) 直書き・typedef 経由・struct メンバ経由とも対応済み |
| 間接呼び出しの fp 実引数→整数仮引数変換 (funcptr / typedef funcptr / struct member funcptr) | 🔧 | funcptr_fp_to_int_arg | (関数ポインタ節参照) 直書き・typedef 経由・struct メンバ・配列要素、int/long、FP 戻り混在まで対応済み |
| 可変長引数（int） | ✅ | (probe q1) | |
| 関数ポインタ経由の可変長呼び出し `int(*f)(int,...); f(...)` | 🔧 | variadic_via_func_pointer | 可変長引数がレジスタ渡しされ Apple ARM64 ABI (stack 渡し) 違反で va_arg がゴミ。直接呼び出しは正常。funcptr lvar に is_variadic_funcptr+固定引数数を記録し経由呼び出しでも variadic ABI を選択 |
| **グローバル可変長関数ポインタ経由の呼び出し** `int (*fp)(FILE*, const char*, ...)=&f; fp(...)` | 🔧 | global_variadic_funcptr_call | ローカル funcptr は上記で修正済みだが、トップレベル declarator の関数サフィックスが skip_balanced_group のみで `...` を解析せず global_var に is_variadic_funcptr が立たなかった。skip_func_params 経路に統一し IR build で ND_GVAR callee も variadic ABI を選択。c-testsuite 00189 |
| volatile スカラ/ポインタの値（`volatile int`, `*p+=`） | ✅ | (probe u3) | 値の正しさのみ。順序保証は別 |
| 文字/エスケープリテラル（`\n` `\0` `\x41` `\101`） | ✅ | (probe u4) | |
| char/short 実引数の int 昇格（可変長経由） | ✅ | (probe u5) | |
| 非可変長の char/short/float 実引数昇格 | ✅ | (probe prom_check) | float→double 昇格含む |
| `restrict` 修飾ポインタ（値の正しさ） | ✅ | (probe restrict_check) | |
| `_Atomic` キーワード（`_Atomic int`/`long` の値） | ✅ | (probe atomic2) | |
| 同梱 `<stdatomic.h>` | 🔧 | stdheader/stdatomic_ops | **本物の LSE アトミックに更新済** (浮動小数の節「本物の <stdatomic.h>」行を参照)。旧・単一スレッド退化版から置き換え、fetch 系は規格通り旧値返却 |
| ワイド文字/文字列リテラル `L'A'` `L"hi"[i]` | ✅ | (probe wide2) | int/long 値として動作 |
| stddef.h の wchar_t / max_align_t（C11 7.19） | 🔧 | stddef_wchar_t, stddef_max_align_t | wchar_t=int(4B), max_align_t=long double(8/8) |
| 戻り値の暗黙変換（int↔double, char/short/unsigned 切り詰め） | ✅ | (probe ret_conv, ret_conv2) | |
| VLA（1D / 2D / 関数引数 / sizeof 全体） | ✅ | (probe vla1, vla2, vla4) | |
| 2D VLA の行の `sizeof(a[0])` / 内側次元が第1パラメータの 2D VLA 引数 | 🔧 | vla_2d_param_and_row_sizeof | A4: `sizeof(a[0])` が要素サイズ→行ストライドスロットを runtime ロード。A5: 内側次元が第1パラメータ (offset 0) の `int a[n][n]` で emit_vla_row_stride_for_params が src_offset==0 を未設定扱いし行ストライド計算を飛ばし SIGSEGV→frame_off 判定に変更 |
| pointer-to-VLA `int (*p)[m]` | 🔧 | pointer_to_vla | ローカルは E3064 で拒否、仮引数 `int (*a)[n]` はコンパイル通過するが行ストライドを実行時 n で計算せずサイレント miscompile していた。行ストライド (extent*elem) を保持する隠しスロット (vla_row_stride_frame_off) をローカル (宣言子で VLA 次元を式捕捉し 16B 確保＋init_chain にストライド store 注入) と仮引数 (既存 register_vla_array_param 機構を再利用、関数 entry で *[rs]=*[n]*elem) の双方で確保。subscript は既存 vla_rsf 経路、ポインタ算術 (+/-) と inc/dec (++/--) はスロットから実行時ストライドを load してスケール。int/double/float/struct 要素・read/write・`(*(p+1))[j]`・p++ 網羅 |
| **3D VLA** `int t[n][m][k]` | 🔧 | vla_3d | register_vla_lvar_and_append_alloc が 1D/2D のみ対応で、3 段目を parse_decl_skip_constexpr_array_suffixes で消費しようとして非定数を拒否し E3064。32B descriptor slot ([base][byte_size][outer_stride][mid_stride]) に拡張し outer=m*k*elem を既存 rsf 経路で、mid=k*elem を別 STORE 注入で初期化。lvar_t に vla_mid_stride_frame_off 追加。build_subscript_deref が ND_LVAR (3D VLA) の subscript 結果 ND_DEREF に vla_row_stride_frame_off=mid_slot を立て、次 subscript が runtime mid stride で動作するように。make_subscript_scaled_offset が ND_DEREF からも vla_rsf を読む。subscript_base_address_of が vla_row_stride_frame_off>0 の deref を address として返す (これがないと t[i] が 1 バイト load されて SIGSEGV)。sizeof(vla3d[i][j]) は vla_mid slot を読む特別経路を追加。all-VLA `int t[n][m][k]`・first-dim VLA `int t[n][3][4]`・double 要素・read/write・3 段 sizeof を網羅 |
| **混在 const/VLA dim** `int t[2][n][4]` 等 | 🔧 | vla_mixed_dims | 第 1 dim が const でも後の dim が VLA なら配列全体は VLA (C11 6.7.6.2)。const 第 1 dim の場合は register_multidim_array_lvar 経由で parse_decl_constexpr_array_suffix_product_n が VLA dim を非定数として E3064 で拒否していた。decl_peek_trailing_array_dims_have_vla で後続 `[...]` を token peek し、TK_IDENT (enum 定数以外) があれば VLA 経路へ redirect。const 第 1 dim を ND_NUM ノードに包んで size_node として register_vla_lvar_and_append_alloc に渡す。2D `int a[2][n]`・3D `[C][n][C]`・`[C][C][n]`・全 VLA・enum 定数 dim を網羅。enum 定数は psx_ctx_find_enum_const で識別して定数扱いし、誤検出を避ける (誤検出した場合も VLA 経路の const-inner 分岐で正しく動く) |
| **4D+ VLA** `int t[n][m][k][l]...` (最大 8 次元) | 🔧 | vla_4d_and_higher | 続き30 の 3D 用 vla_mid_stride_frame_off を汎用 vla_strides_remaining + 連続 stride スロットへ置換。descriptor slot = 16 + 8*(N-1) バイト、stride[k] = dim[k+1]*...*dim[N-1]*elem を slot+16+8*k に保存。level 0 は VLA_ALLOC の rsf 経路で、level 1..N-2 は init_chain への STORE 注入。lvar_t / node_mem_t に vla_strides_remaining を追加し subscript chain で carry。inner_deref_size に elem を立てて連鎖伝播することで、最終 runtime stride 消費後も subscript_base_address_of が「中間配列」を認識し続ける。sizeof(vlaN[i][j]...[d]) は連続 [...] を D 段 peek して slot+16+(D-1)*8 を読む統一経路。4D 全 VLA、4D mixed const/VLA、5D 全 VLA、4 段 sizeof を fixture で網羅 |
| **3D+ VLA 仮引数** `int t[n][m][k]` / `int t[n][m][k][l]` | 🔧 | vla_3d4d_param | parse_param_declarator_name が内側 dim を 2 個までしか捕捉せず 3D 以上は silently 切り捨てられ miscompile していた。修正: (1) 内側 dim を最大 7 個 g_param_inner_dim_consts/idents に保存、(2) lvar_t に vla_param_inner_dim_consts/src_offsets/count を追加、(3) register_vla_array_param で N-D 用 stride スロット (n_inner*8 バイト) を anon lvar `__rs_<name>` として確保し vla_strides_remaining = n_inner - 1、(4) emit_vla_row_stride_for_params で各 level の stride を後ろから 1 MUL で計算して slot+8*level に store。subscript chain は local N-D VLA と同じ機構 (vla_row += 8 / strides_remaining -= 1) を共用。3D 全 VLA・4D 全 VLA・4D mixed const/VLA・3D 全 const 内側を fixture で網羅 |
| **局所 VLA のタグ carry** `struct P arr[n]; arr[i].m` | 🔧 | vla_struct_local | register_vla_lvar_and_append_alloc の呼び出し元が psx_decl_set_var_tag を呼んでおらず、tag_kind が EOF のままで `arr[i].m` の `.` が E3005 で弾かれていた。VLA 1D の `!size_ok` 分岐 + mixed const/VLA redirect 分岐の両方で psx_decl_set_var_tag(var, ..., 0) を追加。struct 直接アクセス・union・ポインタ取得経由・2D mixed `struct P arr[2][n]` を fixture で網羅 |
| **extern global の GOT 経由参照** `extern T v; v;` | 🔧 | extern_global_got | `extern T var;` で宣言のみのグローバル変数 (典型は libc の `__stderrp`) が @PAGE/@PAGEOFF 直参照されてリンク時に "does not have address" で失敗。続き4 の関数アドレス GOT 化と同じパターン。修正: ir_builder に共通ヘルパ emit_load_sym_for_gvar を追加し、psx_find_global_var で gv_ent を引いて is_extern_decl が立っていれば IR_LOAD_SYM の is_got_funcref を立てる (codegen は @GOTPAGE/@GOTPAGEOFF を使う)。LOAD_SYM 発行サイト 5 箇所をヘルパに置き換え。stdio.h に stdin/stdout/stderr (= Apple libc の __std{in,out,err}p) を追加 |
| **`struct N **` 仮引数の `(*root)->m`** | 🔧 | struct_pp_param_arrow | 多段の struct ポインタ仮引数 `struct N **root` で `(*root)->m` が E3005 で弾かれていた (ローカル `struct N **root` は動作)。register_param_lvar の struct ポインタ分岐 (param_ptr_levels>=2) で pointer_qual_levels が立っていなかったため、build_unary_deref_node の `*root` で is_tag_pointer 伝播が pql>=2 を要求して 0 にクリアされ、続く `->` が base_is_ptr=0 で弾かれていた。修正: param_ptr_levels>=2 のとき var->pointer_qual_levels = param_ptr_levels を設定 |
| **ファイルスコープの `T *p = (T[]){...}`** | 🔧 | file_scope_ptr_from_array_compound | ポインタ変数を配列複合リテラルで初期化すると SIGBUS。apply_toplevel_object_initializer の strip heuristic が `(T){...}` を無条件で剥がして `T *p = {...}` (複数値で初期化) と解釈し、先頭要素値がポインタスロットに書き込まれていた。修正: 集約 (配列 / struct 値 / union 値) のときだけ strip し、ポインタ・スカラ var では式経路 (psx_expr_assign) で compound literal を hidden gvar に materialize させる。ただし `char *p = (char[N]){"str"}` の単一文字列形は等価なので peek で例外的に strip 許可。int*/unsigned char*/sized array/char* + 単一文字列を fixture で網羅 |
| **関数の宣言と定義でシグネチャ照合** (C11 6.7p4) | 🔧 | function_redecl_signature | 戻り型のみ照合 (psx_ctx_track_function_ret_type) で、引数数 / 引数型の不一致は素通しだった。具体的には `int g(int); int g(int x, int y)` (引数数違い) や `int h(int); int h(double x)` (引数型違い) が silently 通過し、後段で誤 codegen の原因に。修正: (1) psx_ctx_track_function_nargs を追加し初回登録/以降比較で引数数と可変長性を照合、(2) psx_ctx_track_function_param_category を追加し引数の粗粒度カテゴリ (INT/FLOAT/DOUBLE/PTR/STRUCT/UNSET) で照合。INT/LONG は同カテゴリ扱い (proto の placeholder ND_NUM は sz=4、def の ND_LVAR は abi_type_size=8 で本来一致しないため、整数幅は別追跡が要る後続課題)。fixture は合法な再宣言を網羅し、回帰がないことを保証 |
| **関数の重複定義検出** (C11 6.9p3) | 🔧 | function_duplicate_def | 同名関数の本体定義を 2 度書く `int f(){...} int f(){...}` が silently 通過し、後段でアセンブラ/リンカが duplicate symbol を出すまで気づけなかった。修正: func_name_t に is_defined フラグを追加し、funcdef の本体パース直前 (proto `;` を弾いた後) で psx_ctx_track_function_defined を呼ぶ。初回は記録、2 度目なら E3064。プロトタイプは何度書いても合法 (本フラグは立たない)。proto+def 混在・static 関数・複数 proto + 1 つの定義の合法形を fixture で網羅 |
| **declaration-specifier 順序自由 / storage class 重複 / グローバル重複定義** | 🔧 | decl_spec_order_and_dup | 3 件の関連バグ: (1) `int static x` (型 → storage class 順) が E3016 で拒否されていた。C11 6.7p1 で declaration-specifiers の順序は任意。psx_consume_type_kind のループ内で型指定子の後ろに出現する storage class / qualifier を消費し flag を立てるよう拡張。(2) `int static static`、`static int static`、`static int extern` 等の interleaved storage class 重複 / 併用が見逃されていた (skip_cv_qualifiers の storage_count は単発呼び出し内のみ)。同じくループ内でグローバル状態 (g_last_decl_is_static / is_extern) をチェックして 2 度目で E3064。(3) グローバル `int g=1; int g=2;` の重複定義 / `int g; double g;` の型違いが silently 通過し後段でアセンブラの duplicate symbol で気づくのみだった。register_toplevel_global_decl で同名既存を merge (extern と同様、型互換チェック付き)、apply_toplevel_object_initializer で `=` 消費時に既存 has_init を検出して E3064。tentative def 同型 (`int g; int g;`) は合法 merge、qualifier 重複 (`const const int x`、C11 6.7.3p5) は合法、各種順序の混在を fixture で網羅 |
| **識別子の名前空間衝突** (C11 6.2.3 / 6.7p4) | 🔧 | name_namespace_collision | 続き39 で global var redef は検出したが、(a) `extern int g; double g = 1.5;` (extern と定義の型不一致)、(b) `int foo(int){...} int foo;` (関数→変数)、(c) `int bar; int bar(int){...}` (変数→関数)、(d) `typedef int T; int T = 5;` (typedef→変数) はそれぞれ silently 通過。register_toplevel_global_decl の merge ロジックを extern 経由でも走るように一本化 (extern と非 extern の両方で型互換チェック)、加えて psx_ctx_has_function_name と psx_ctx_find_typedef_name で名前衝突を検出。funcdef では find_global_var_by_name で同名グローバル変数の有無を確認。合法形 (同型 extern+定義、proto+def、typedef を変数の型として使用) を fixture で網羅 |
| **追加の識別子診断** (関数代入 / enum 衝突 / implicit function declaration) | 🔧 | identifier_diagnostics | 続き40 の延長として 3 件: (1) `f = 5;` の関数識別子への代入が "ir build/emit failed" 粗エラーで止まっていたのを、assign 関数の前で ND_FUNCREF を check し parser 段で明確な E3064 を出すように。(2) `enum E{A=5}; int A=10;` および逆順 `int B=10; enum E{B=5};` の enum 定数と通常 identifier の名前空間衝突を検出 (register_toplevel_global_decl と enum_const.c の双方で全 4 名前種を check)。(3) C99/C11 で禁止されている implicit function declaration `undecl_func()` が silently 通過していたのを W3001 warning として警告 (build_unqualified_call で psx_ctx_has_function_name / psx_find_global_var に見つからない場合)。fixture では合法形 (関数 proto + 呼び出し / アドレス取得、enum 定数を含む switch) の回帰確認 |
| **タグ再定義検出 / 非 void 関数の return なし** | 🔧 | tag_redef_and_return | (1) `struct S { int x; }; struct S { int y; };` のタグ再定義が silently 通過。psx_ctx_define_tag_type_with_layout で同一スコープに既存の完全型 (member_count > 0) があり今回も完全型なら C11 6.7.2.1p1 違反 E3005。前方宣言→定義 / 内側スコープ shadow は従来挙動を保持。enum タグも同じ機構で検出。(2) `int get(int){ x = x+1; }` のように非 void 関数で値を返さずに終端するのが silently 通過 (C11 6.9.1p12 未定義動作)。emit_implicit_return_if_missing で main 以外の非 void 関数では W3001 warning (main は C11 5.1.2.2.3 で暗黙 return 0 が標準化されているので例外)。副次: ps_program_from 冒頭で psx_ctx_reset_tag_diag_state / reset_function_diag_state を呼び、ユニットテスト用のソフトリセットを追加 (実コンパイルは 1 ファイル 1 プロセスなので影響なし) |
| **コンパイル時 UB 警告 / 怪しい書き方** | 🔧 | undefined_behavior_warnings | 4 件の W3001 warning を追加: (1) 0 リテラルでの除算・剰余 `1 / 0` / `1 % 0` (C11 6.5.5p5 UB)。mul() の ND_DIV / ND_MOD 構築時に rhs が ND_NUM(0) なら警告。(2) シフト量が型の幅を超える `1 << 32` / `1 << -1` (C11 6.5.7p3 UB)。shift() で rhs が ND_NUM で int=32/long=64 以上または負なら警告。(3) 自己代入 `x = x` (clang -Wself-assign 相当)。assign() の TK_ASSIGN 分岐で両辺が同じ ND_LVAR offset なら警告。(4) 空 if 本体 `if (cond);` (clang -Wempty-body 相当)。parse_stmt_if で `)` 直後が TK_SEMI なら警告。fixture では非ゼロ除算 / 適切なシフト / 別変数への代入 / 本体ありの if で false-positive なしを確認 |
| **`const struct` / `const struct *` / const struct 配列要素のメンバ代入拒否** `const struct S s; s.x=2` / `const struct S a[1]; a[0].x=2` / `const struct S (*p)[1]; (*p)[0].x=2` | 🔧 | compile_fail: const_struct_member_assign_rejected, const_struct_array_member_assign_rejected, global_const_struct_array_member_assign_rejected, const_struct_pointer_to_array_member_assign_rejected | stmt.c の tag-keyword fast path が `const struct S s` / `struct S const s` / inline tag の const を after_type に渡さず、さらに `s.x` の ND_DEREF 代入で const を見ていなかった。tag 経路で const/volatile を保存し、メンバ deref に親 const を伝播、const 付き ND_DEREF への代入を E3077 にする。続き103で配列 decay の `ND_ADDR` と subscript 結果 `ND_DEREF` に const/volatile が伝播しない漏れを修正し、ローカル/static local/global の const struct 配列要素メンバ代入も E3077 にする。続き104で単項 `*` の結果に pointee const/volatile が伝播しない漏れを修正し、pointer-to-array 経由の const 配列要素メンバ代入も E3077 にする。非 const struct メンバ代入と `struct S * const p` 経由の mutable member 代入は許可 |
| **縮小変換と自己比較** | 🔧 | narrowing_and_self_compare | 2 件の W3001 warning を追加: (1) 整数変数を浮動小数点リテラルで初期化 `int x = 1.5;` (clang -Wliteral-conversion 相当)。psx_decl_parse_initializer_for_var のスカラ非ポインタ非タグ分岐で init_expr が ND_NUM かつ fp_kind != NONE かつ fval に小数部 (fval != (long long)fval) があれば警告。整数値の暗黙変換 (`int x = 2.0;`) は値が等価なので警告しない。(2) 自己比較 `x == x` / `x != x` (clang -Wtautological-compare 相当)。equality() で両辺が同じ ND_LVAR offset または同じ ND_GVAR 名なら警告。fixture では整数代入 / float 代入 / 明示キャスト経由 / 異なる変数同士の比較で false-positive なしを確認 |
| **代入を条件式 / 整数オーバーフロー / dangling pointer** | 🔧 | assign_overflow_dangling | 3 件の W3001 warning を追加: (1) `if (x = 10)` / `while (x = 0)` の代入を条件として使う形 (clang -Wparentheses 相当)。parse_stmt_if / parse_stmt_while で条件式 top が ND_ASSIGN なら警告。意図的形 `while ((c=getchar()) != EOF)` は top が ND_NE なので発火しない。(2) 整数リテラルが型サイズを超える初期化 `char c = 300;` (clang -Wconstant-conversion 相当)。decl.c のスカラ初期化分岐で var->elem_size < 4 かつ ND_NUM の値が型範囲外なら警告。`unsigned char uc = -1;` は全ビット 1 のイディオムとして除外。(3) ローカル変数アドレスの return `return &x;` (clang -Wreturn-stack-address 相当)。parse_stmt_return で ND_ADDR(ND_LVAR) かつ static でなければ警告。static ローカル / グローバル / 引数渡しは合法 |

### リンケージ / 複数 TU（extern / static）
複数ファイルをリンクする差分ハーネス（各 .c を ag_c で .s 化→clang で個別アセンブル→
まとめてリンク、clang -I include 直ビルドと exit code 比較）で確認。
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| extern グローバル変数 int/double/struct/`arr[]`/ポインタ の TU 跨ぎ参照 | ✅ | — | 値・型とも clang と一致 |
| extern 関数の TU 跨ぎ呼び出し（int/double 戻り） | ✅ | — | |
| tentative definition `int t;` を複数 TU | ✅ | — | `.comm` で正しくマージ |
| **ファイルスコープ `static` 変数/関数の内部リンケージ** | 🔧 | static_internal_linkage | C11 6.2.2p3 違反。ag_c が `static` に `.global` を出し、無初期化 static を `.comm`（common=外部）にしていたため、同名 static を持つ別 TU と `duplicate symbol` 衝突／共有。codegen で static は `.global` 抑制＋無初期化を `.zerofill __bss`（ローカル）に。グローバル/関数/関数内 static の 3 経路。is_static フラグを global_var_t / ir_func_t / node_func_t に追加し parser から伝播 |
| **extern 宣言＋同一TU定義（tag/typedef 基底）** `extern struct S es; struct S es={7};` | 🔧 | extern_then_def_same_tu | C11 6.9.2。builtin (`extern int v; int v=5;`) は動作。2 つの独立バグ: (1) storage class フラグ (g_*_is_extern/static) が宣言間でリセットされず前の extern が次の bare-struct 定義に漏れ→finalize の extern 分岐が brace を scalar 式として食べ E3064。reset_toplevel_decl_spec_state と parse_toplevel_tag_decl で宣言ごとに 0 クリア。(2) typedef object 経路 (apply_toplevel_typedef_prefix_flags) が extern を無条件 0 にし `extern T et;` が tentative 定義 (.comm) になり本定義 data と重複 ASSEMBLE_FAIL→extern/static を伝播 (漏れは (1) の reset で防止)。static の漏れ (`static struct S a; struct S b;` で b が内部リンケージ化) も解消。逆順 (定義→extern)・別 TU は元から動作。tag/typedef/ポインタ/配列・複数宣言子を網羅 |

---

## バグではない（仕様 / 既知の差異、追わない）
- **ag_c は標準C のみ対象。GNU 拡張は新規に実装しない。差分テストで clang が GNU 拡張として受理するものは「バグ/未対応」として扱わない。**
  - `, ##__VA_ARGS__`（GNU 可変長マクロのカンマ削除）— 未対応のまま。
  - statement expression `({...})`、`typeof`、範囲指定子 `[2 ... 5]=v`、二重波括弧スカラ初期化 — 標準外。
  - 認識済みの未対応 GNU 拡張は `W3024` で「このコンパイラでは使用できない」旨を警告して読み飛ばす。
    `#pragma push_macro` / `pop_macro` は意味実装せず行を skip、範囲指定子 `[lo ... hi]=v` は先頭 `lo`
    の単一 designator として処理するのみ (GCC/clang 互換の範囲 fill ではない)。ゼロ長配列 `[0]` は
    0 バイトとして処理する。
  - （既に入っている GNU 拡張 `__real__`/`__imag__` は complex.h が依存するため残置。新規は入れない。）
- 過剰初期化子 `struct S s={{1,2},{3,4}}`（メンバ1個に2グループ）等は ag_c は意図的に E3064（厳格）。
- `s07.c`（深さ 10 万の再帰）の SIGSEGV はスタックオーバーフロー（誤コンパイルではない）。
- 評価順序が未規定/UB のもの（`a[i++]=i` 等）。

## 未チェックの着手候補（⬜ を埋める）
（現時点で未着手候補なし。下記「既存バグ」も含めすべて対応済み。）

### ストリーミング前方先読み境界バグ（既存バグ・修正済み）
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| `_Generic` 型照合の `t->next` 先読みがストリーミング窓境界で NULL を踏む | 🔧 | generic_streaming_lookahead | `_Generic` の型照合はカーソルを進めず `t->next` で型全体を先読みする。ストリーミング生成器はカーソル前方を一定窓だけ materialize し、`set_curtok` のジャンプが補充起点 `refill_at` を飛び越えると補充がチェーン末尾到達まで起きない。複雑なグローバル funcptr/ネスト宣言 + 複数の `assert(_Generic)`（`#expr` 文字列化で大量トークン）+ 末尾の cast 制御式の深いネスト型が同居すると、窓境界が型の途中に来て先読みが未生成境界(NULL)を踏み、有効な型を誤却下し「':' が必要 (実際 '(')」E2006。**72b23c2 でも再現する既存バグ**で型シグネチャ照合とは独立。チャンク再利用は無関係(無効化しても再現)。修正: 深い前方先読みの直前に `tk_ensure_lookahead()`（プリプロセッサが登録するフック経由で窓を満たす、parser↔preprocess は疎結合のまま）を呼ぶ |

## チェック済みだが miscompile でなかった領域（再探索不要）
- compound literal: ネスト / `&(compound)` / 配列 / 関数引数 (probe cl1, cl3)。
- `static p = &(struct){..}`（非定数 static init）は clang が拒否、ag_c は受理（寛容な差異）(probe cl2)。
- 浮動小数の `printf` 出力書式・丸め（`%f`/`%g`/`%e`/`%E`、精度・幅・フラグ、`%.0f` の偶数丸め、
  `0.0`/`-0.0`/Inf/NaN、`float`→`double` 可変長昇格、int/float 混在のレジスタ/スタック境界、
  `snprintf`）を stdout 比較で確認。書式・丸めは libc 任せで ag_c は引数を正しく渡しており全て一致。

### 2026-06-22 セッションの探索（~84 probe、すべて clang 一致 = miscompile なし）
2D ポインタ配列 4 象限を修正した後の網羅探索。下記領域は stdout/exit code 比較で全 green。
**再探索不要**（バグは依然「宣言子・型の複雑な組合せ」に集中し、式・制御フロー・ABI・数値変換は堅牢）:
- **複合代入**: `%= <<= |= ^= &= += -= /=` を int/ポインタ/`unsigned char`/`long`・負値で。ポインタ複合代入 `p+=3; *p+=10`。
- **ビット演算の幅**: `unsigned<<long`、`(long)x<<40`、`u>>31`、補数。
- **enum × bitfield**: `enum 値 | で初期化した unsigned:3` 等。signed bitfield 抽出（`int a:4=-1`）、バイト跨ぎ bitfield（`:6,:6,:6`）、bitfield 代入オーバーフロー（`:4 に 20`）。
- **整数昇格/変換**: char/short 実引数→int、float 実引数→double、narrowing（long→int→short→char 連鎖）、signed↔unsigned、`(int)(char)(long)double` キャスト連鎖、戻り値の暗黙切り詰め（`char f(){return 0x141;}`）。
- **ABI**: >8 整数引数（スタックスピル）、int/float 混在引数、>8 float 引数、struct 引数と scalar 混在、浮動小数レジスタ枯渇スピル。
- **三項/論理**: ternary がポインタ/struct を返す、ネスト三項、`!`/`!!`、短絡、`_Bool` 論理。
- **ポインタ/配列境界**: 配列⇄ポインタ仮引数、負の添字 `p[-1]`、2D の flat 添字 `(&a[0][0])[4]`、ポインタ差/比較/等価。
- **宣言子/typedef**: VLA（1D/2D、`sizeof`、多次元 VLA 引数）、typedef の fp/配列/多段ポインタ/qualified、`int(*p)[3]` 仮引数、const ポインタ vs ポインタ to const、関数を返す関数ポインタ、`int(**pp)(void)=t` 経由呼び出し。
- **集約初期化**: 指示初期化子 `[n]=`（飛び/混在）、複合リテラル `(struct){.x=1,.z=3}`・`(int[5]){[2]=..}`、部分初期化、ネスト struct/union、匿名 struct/union メンバ、flexible array member の sizeof。
- **グローバル初期化**: アドレス+オフセット `&arr[2]`/`arr+3`、定数式 `{1*1,2*2,..}`、struct 内アドレス `{&g,7}`、グローバル 2D 配列。
- **その他**: const struct コピー、static ローカルのアドレス返し、再帰（accumulator/相互/Ackermann は別途確認済み）、連鎖代入 `a=b=c=5`、深いメンバ連鎖 `d.c.b.a.v`、struct 配列メンバのループ、ネスト関数呼び出し引数、volatile 再読み込み、文字列/文字エスケープ（`\t \\ \" \x41 \101 \0`）、文字列連結、除算/剰余の符号全組合せ、シフト端、octal/hex/binary リテラル、`unsigned long long` サフィックス、プリプロセッサ stringize/paste、`#include <string.h>`(strcmp/strlen/memset)。

### 2026-06-22 セッション 続き: 大きめプログラム・libc 連携の探索（全 green、1 件のみ別途修正済み）
最適化が絡む大きめプログラム・libc 連携・レジスタ圧迫を狙った探索。下記は全一致で**再探索不要**
（唯一 global_struct_char_array_member だけ miscompile で別途 🔧 修正済み）:
- **アルゴリズム**: 3x3 行列積、bubble/qsort（int 配列・struct 配列）、popcount ループ、文字列反転、
  二分木（malloc/再帰 sum）、ハッシュテーブル（線形探索 put/get）、bignum 桁加算、状態機械（switch）、
  Duff's device、gcd 二重ループ、ポインタ追跡（idx[]）、固定小数点乗算（`(a*b)>>16`）。
- **libc 連携**: malloc/free、qsort+比較関数、strtok/strlen、memmove（重複領域）、math（1/i² 累積）。
- **レジスタ圧迫**: 16 整数ローカルの混合演算、10 double の混合演算、4 重ネストループ、2D 転置。
- **グローバル集約**: 混在メンバ struct `{int,int,double,char[8],int[4]}`（char[] メンバ含む形が唯一のバグ）。

### 2026-06-22 セッション 続き17: 探索 round 2 (タグ shadowing 完了後)
タグ shadowing 応用形修正後、未探索の角度で probe を 19 件流して下記領域は全て clang 一致 (=
**再探索不要**)。新規 2 件は別途記録 (HANDOFF.md「次セッションの最優先タスク」参照):
- **libc string**: strcpy/strcat/strcmp/memset/memcpy/strlen の組合せ
- **libc math**: sqrt/sin/cos/floor/ceil/fabs/pow/log
- **libc malloc/free**: malloc + 関数引数渡し、strcpy(malloc(16),...)
- **libc qsort**: 関数ポインタ callback (cmp_int)
- **可変長関数**: va_list で int 列 / double 列を sum
- **typedef + 関数ポインタ**: シンプルな `typedef int (*BinOp)(int,int); BinOp ops[3] = {...}` は OK。
  ただし「関数ポインタ配列へのポインタ」(`BinOp (*pa)[3]` で要素が関数ポインタ) は 🆕 (未修正)。
- **bitfield + union**: union { unsigned int v; struct { :8, :8, :16 } } のパッキング
- **文字列リテラル連結**: `"hello " "world"` の concatenation + sizeof
- **16進浮動小数リテラル**: `0x1.8p1` / `0x1p-3` / `0x1.fffp10`
- **VLA + 多次元**: 関数引数 `int a[n][m]` + sum_n
- **三項 + struct/struct ポインタ/配列**: `cond ? a : b` で struct 値・ポインタ・配列
- **static ローカル struct**: 関数呼び越し永続化 (`static struct Counter c = {0, 0.0}` + 累積)
- **再帰 struct (リスト)**: malloc でリスト構築 + sum 累積
- **const struct**: ポインタ経由のアクセス、グローバル const struct
- **offsetof**: char/int/double/配列メンバの offsetof マクロ
- **配列の関数引数 decay**: `int arr[10]` 仮引数の sizeof
- **short ポインタ算術**: short 配列の `p+2`/`p[4]`/`p-arr`
- **function-local static counter**: `static int id = 0; return ++id;`
- **ネスト struct + union メンバ designator (int)**: `{ .n = 99 }` は OK。
  ただし `.f = 2.5f` (float メンバ) は 🆕 (未修正、ネスト union の fp 初期化バグ)。

### 2026-06-22 セッション 続き23: 探索 round 3 (12 probe、新規 2 件発見・修正)
タグ shadowing 完了後、未探索の角度で probe 12 件流して下記領域は全て clang 一致 (=
**再探索不要**)。新規発見の 2 件は続き23 で同時修正:
- **C11 機能**: _Noreturn (stdnoreturn.h)、_Thread_local (TLS 初期化と書き込み)、
  _Static_assert (関数内・トップレベルは元から OK)
- **言語機能**: enum + ビット演算組合せ、部分初期化 (残り 0 埋め、global/local/struct)、
  多次元 const 配列、多引数呼び出し (>8 整数/fp 混在 stack spill)、struct 戻り関数のチェーン
  (`add(make(1,2), make(10,20))`)、グローバルアドレス算術 (`data + 5`、`&data[9]`)、
  libc string 関数の strchr 経路、bitfield + _Alignas 配置と offsetof (非 bitfield メンバ)
- **新規発見 (続き23 で修正済み)**:
  - `typedef IP IPA[3]; IPA arr = {&x, &y, &z}` (pointer-element 配列 typedef の宣言)
    →続き23 で base_is_pointer + 配列 typedef のとき is_pointer=0 リセットして配列宣言経路に。
  - `struct S { _Static_assert(...); int x; };` (struct メンバ位置の static_assert)
    →続き23 で struct_layout に TK_STATIC_ASSERT 分岐追加。
- **C 仕様外で probe から除外**: offsetof on bitfield (clang もエラー、未定義動作)。

### 2026-06-22 セッション 続き24: 探索 round 4 (12 probe、新規 2 件発見・修正 + 3 件残課題)
未探索角度 12 件流して新規 2 件発見・修正 + 3 件残課題を HANDOFF 記録:
- **C11/言語機能 (green、再探索不要)**: typedef of struct (self-referential)、_Generic 細形
  (char*/const char*/int* 区別)、複合代入 `<<=` `>>=` `|=` `&=` 各種 + struct メンバ配列、
  snprintf format flags (`%05d %-8s %+d %.3f %x %o %*d`)、realloc + 動的配列、
  深いネスト (3 重 for + 条件 + 集約)、長い二項演算子チェーン (20 項加算/シフト)、
  struct 配列内 char 配列 + 文字列初期化 (`{5, "hello"}` × 3)
- **修正 (続き24)**:
  - `void *p = (void*)0xdeadbeefL` (明示キャスト経由のポインタ初期化) → node_num_t に
    from_pointer_cast フラグを追加し apply_cast でスタンプ、init check で skip。
  - グローバル `struct P *parr[3]; parr[i]->x` (struct ポインタ配列の subscript + ->)
    → try_build_global_var_node で is_tag_pointer のとき pql=1/bds=struct サイズを立て、
    emit を scalar 経路に流す。
- **残課題** (続き 25/26/27 で全て対応済み):
  - VLA `sizeof(arr)` ランタイム計算: 続き25 で vla_sizeof_direct として修正済み。
    印付き変数経由・variadic 引数経路を網羅。
  - グローバル struct ポインタ配列の init slot 計算: 続き26 で global_struct_ptr_array_subscript
    拡張として修正済み (gbrace_ctx_t.is_tag_pointer を追加)。
  - `(*pp)->x` (struct P** の単独 deref からの ->): 続き27 で *(ptr-to-array) tag carry
    の文脈で解決済み。

### 2026-06-22 セッション 続き32: 探索 round (20 probe、全 green)
3D VLA + 混在 const/VLA 対応後の網羅探索。下記領域は全て clang 一致 = **再探索不要**:
- 関数ポインタ戻り値 / 関数ポインタ配列メンバ + 集約初期化 (FI (*get_picker())(int))
- qsort 複雑 comparator (struct R + tie-break)
- 複合代入チェーン (`s.a[2] += s.a[0] * 3`、構造体メンバ × 配列要素 × shift)
- snprintf format flags 細形 (`#`/`0` フラグ、precision、`e`/`g`/`a` 浮動小数)
- ポインタ配列 + 負添字 + 文字列長合計
- unsigned long ビット演算 (64bit 64bit 混在 long * 0x9E3779B97F4A7C15UL)
- 可変長引数 double 8 個交互加減算
- bitfield + cast (`(unsigned)(signed bitfield)`)
- 関数ポインタ配列 struct メンバ + 集約初期化 (`struct Engine { int(*ops[3])(int,int); }`)
- 混在幅比較 (short+ushort、char==255)
- 再帰 struct list (malloc/free chain + sum)
- switch fallthrough (4-way fallthrough + default)
- designator init array gap (`[1]=1,[3]=3,[5]=5` + positional 混在)
- extern + 同一 TU 定義 (int/double)
- マクロ stringize / paste の組合せ (`VAR(1)`, `MAKE_STR_PAIR(name, val)`)
- volatile + post-increment 副作用
- const 関数ポインタ typedef (`typedef int (*const Cmp)(...)`)
- goto + label の複雑な制御フロー
- ternary が struct 値を返す (`b ? (struct V){...} : (struct V){...}`)
- 大きい struct 値渡し + scalar 混在 (struct{8int+3double} × 2 + int/double)
- **GNU 拡張で probe 除外**: inline 関数の暗黙的 linkage 解釈の細形 (probe `u_inline.c` は
  clang もエラー)。
