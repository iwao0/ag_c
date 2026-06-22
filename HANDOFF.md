# HANDOFF — ag_c バグ修正セッション

最終更新: 2026-06-21（探索的差分テスト継続セッション）

## このセッションの目的
clang との差分テスト（同一 C ソースを ag_c と clang でコンパイルして exit code を
比較）で ag_c の miscompile / コンパイルエラーを炙り出し、修正して回帰テストを追加する。

## 2026-06-21 セッション（続き）: bug_coverage の ⚠️ 全消化 → 探索で宣言子/型バグ 6 件
現在 `make test` = **1013/1013 E2E green**（unit/parser/preprocess/IR 含む全 green）。
回帰 fixture は `test/fixtures/probes_found_bugs/`、索引は `docs/differential_testing/bug_coverage.md`。

### 前半: 残っていた ⚠️（既知バグ）を全部 🔧 に
- 多段ポインタの fp pointee `double **p; **p`（b90f302）multilevel_pointer_fp_pointee
- ファイルスコープ集約複合リテラルのアドレス `&(struct S){...}`（d47504b）file_scope_aggregate_compound_literal_addr
- グローバルのネスト brace designator `{.items={[2]={.a=7}}}`（3bbba23）global_nested_brace_designator
- `#if 0` 偽分岐の非C トークン skip（adb180e）if0_skip_non_c_tokens。寛容モード+setjmp/longjmp
- **pointer-to-VLA** `int (*p)[m]`（9fa50c0）pointer_to_vla。ランタイム行ストライド機構を local/param 両方で

### 後半: 探索的差分テスト（/tmp に probe を量産→agc_diff_test）で新規発見・修正
バグは**宣言子・関数戻り型**に集中（式・制御フロー・ABI・プリプロセッサは堅牢で probe 全 green）。
- **ポインタ戻り関数の subscript/算術** `g()[i]` / `*(g()+i)`（df7da63）func_pointer_return_subscript。
  parser がポインタ戻り値の pointee 型を覚えず ND_FUNCALL の deref_size=0 で誤スケール。`double* g()` は
  戻り値を d0 から誤読し SIGSEGV、`unsigned char* g()` は符号拡張。semantic ctx の戻り型から導出。
- **storage class 付きタグ戻り関数** `static struct S *f()`（ea308e3）static_tag_return_function
- **配列へのポインタを返す関数** `int (*f())[N]`（19f81f6）func_return_pointer_to_array。pointee 配列次元を
  捕捉し ret_pointee_array_first_dim に記録。単一次元のみ（多次元 `[N][M]` 戻りは未記録）
- **ファイルスコープ `static <typedef名> 変数`** `static Point p;`（4cdb34b）static_typedef_name_global
- **const/volatile 付きポインタ戻り型** `int *const f()`（9d57b4c）qualified_pointer_return
- **タグ戻り + `(*...)` 宣言子** `struct P (*f())[3]` / `struct R (*f())(int)`（9bd6850）tag_return_complex_declarator。
  is_toplevel/is_tag の宣言子判定を共有ヘルパ is_function_declarator_sig に抽出して統一

### このセッション（続き2）: struct を返す関数ポインタの間接呼び出しを 2 件修正
- **struct を返す関数ポインタの間接メンバアクセス** `op(41).v` / `op(41)->v`（9f1ac04）funcptr_return_struct_member。
  間接呼び出し（callee != NULL）の ND_FUNCALL に戻り tag 型が伝播せず psx_node_get_tag_type が
  TK_EOF を返し E3005。callee の funcptr 変数（tag フィールドに戻り tag を保持）から導出し、戻り値が
  ポインタか否かは pointer_qual_levels で判定（値戻り pql1→ptr0 / ポインタ戻り pql2→ptr1）。
  値戻り/ポインタ戻り・deref形 `(*op)(x).v`・global funcptr・union 戻り・8B ネストメンバ連鎖を網羅。
- **1/2/4/8B 以外（非 pow2）の struct を返す関数ポインタの間接呼び出し**（ca5af1f）funcptr_return_large_struct。
  x8 ret_area 間接返し ABI が direct call 限定で、間接は IR build 失敗（メンバアクセス以前に
  `struct Big r=ob(100);` 単独でも）。3 箇所修正: (1) parse_call_postfix が間接 funcall に ret_struct_size を
  未設定→callee 戻り tag（pql<=1）からサイズ導出。(2) build_assign_struct の間接 fail を汎用 funcall 経路へ
  委譲し ret_area→dst memcpy。(3) build_node_funcall の ret_area 確保 `!fn->callee` ゲートを撤廃。
  12B/16B/20B struct・16B union・代入・直接メンバ・deref形・global・値引数渡しを網羅。codegen は元から
  x8 設定と blr を独立に出せていた（IR builder 側のゲートのみが原因）。
- **多段ポインタを返す関数の直接 deref** `int **g(); **g()`（d5ceb72）multilevel_pointer_return。
  semantic ctx の ret_is_pointer が bool（段数なし）で `int **` を単段 `int *`（pointee 4B）扱い→
  `*g()` が int になり `**g()` が int 値をアドレス参照で SIGSEGV。型付き変数経由は元から可。戻り型の
  `*` 段数を ret_pointer_levels に記録し、node_utils の funcall 経路（pointer_qual_levels /
  base_deref_size / ps_node_deref_size）が段数>=2 のとき `*g()` を「1 段減らしたポインタ」として組む。
  build_subscript_deref も funcall base を 1 段消費（`g()[i]`→int*）。int**/char**/int***・prefix deref・
  deref+subscript 混在 `(*rg())[1]`・直接 subscript `rg()[0][i]` を網羅。単段ポインタ戻りは不変（段数>=2 ゲート）。

### このセッション（続き2）の追加修正
- **extern 宣言＋同一TU定義（tag/typedef 基底）** `extern struct S es; struct S es={7};`（da88075）
  extern_then_def_same_tu。2 つの独立バグ: (1) storage class フラグ（g_*_is_extern/static）が宣言間で
  リセットされず、前の extern が次の bare-struct 定義に漏れ→finalize の extern 分岐が brace を
  scalar 式として食べ E3064。reset_toplevel_decl_spec_state と parse_toplevel_tag_decl で宣言ごとに
  0 クリア。(2) typedef object 経路（apply_toplevel_typedef_prefix_flags）が extern を無条件 0 に
  していたため `extern T et;` が tentative 定義（.comm）になり本定義と重複 ASSEMBLE_FAIL。extern/static
  を伝播（漏れは (1) の reset で防止）。static の漏れ（`static struct S a; struct S b;` で b が内部
  リンケージ化）も解消。逆順（定義→extern）・別 TU は元から動作。

### このセッション（続き3）: 探索的差分テストで新規 2 件
HANDOFF 列挙の既知未対応を全消化後、/tmp に ~130 probe を量産して agc_diff_test。大半 green
（complex.h・union 配列 init・変則 ptr2d 等、過去の「既知未対応」は集約初期化修正で解消済みと判明）。
新規 miscompile/コンパイルエラーを 2 件発見・修正:
- **ポインタ typedef 仮引数の subscript** `typedef char* Str; len(Str s){ s[i] }`（4d81c8c）
  pointer_typedef_param_subscript。param_decl_spec_t が typedef のポインタ性を捕捉せず、宣言子に
  `*` が無いためスカラ登録され `s[i]` が E3064。deref `*s` は寛容判定で動作・直書き `const char*` も動作。
  typedef 基底のポインタ段数を捕捉し宣言子の `*` と合成して実効ポインタ性を決める。
- **unsigned char/short ポインタ経由の zero-extend** `unsigned char* p; p[i]`（c6ac5d9）
  unsigned_char_pointer_zero_extend。pointee が符号拡張され 200→-56 に化けた。3 経路を修正:
  (1) local subscript の最終要素判定が単段ポインタ (pql=1) を認識できず → fp 中間行判定と対称な
  `!is_pointer && !(inner_ds>0 && es>inner_ds)` に。(2) 仮引数の pointee unsigned を param_decl_spec_t
  に捕捉し var->is_unsigned へ伝播。(3) `*(p+i)` の ND_ADD/SUB を辿る node_pointee_is_unsigned ヘルパ追加。
  signed は符号拡張維持。
- **グローバルの 2 次元以上のポインタ配列** `int *t[2][2]` / `char *names[2][2]` / `int(*t[2][2])(void)`（320e0ff）
  global_2d_pointer_array。`t[i][j]` が SIGSEGV（非ポインタ `int t[2][2]` は動作）。3 修正:
  (1) apply_global_multidim_strides の `!head.is_ptr` ゲートを外し elem_size=8 で stride を立てる。
  (2) build_subscript_deref の pointee_is_scalar_ptr を最終次元(inner_ds==0)のみ load・中間は伝播し
  要素 pointee サイズを base_deref_size で carry。(3) 括弧内配列 `(*t[2][2])` の個別 dims を
  psx_parse_array_suffixes_capture_dims で捕捉。2D/3D データ・char* 文字列・要素代入・2D funcptr を網羅。
- **ローカルの 2 次元以上のデータポインタ配列** `int *t[2][2]`（ff709ed）local_2d_pointer_array。
  register_multidim_array_lvar が outer_stride を立てるが登録後に pql=1/base_deref_size=4 を立てるため、
  build_subscript_deref の「要素はポインタ」分岐が **1 段目** で発火し deref_size を inner_ds(8) から
  bds(4) に上書き → 2 段目が +4/ldrsw に化けた。fp/unsigned と同じ中間行判定 (inner_ds>0 && es>inner_ds)
  で 1 段目を中間行と認識し pointer-element 化を最終次元まで遅延・pql/bds を carry。2D/3D・char*・代入を
  網羅。1D `int *arr[N]`・genuine `int **pp` は不変。
- **ローカルの 2 次元以上の関数ポインタ配列** `int(*t[2][2])(void)`（cf36337）local_2d_funcptr_array。
  ネスト brace init `{{a,b},{c,d}}` が E3064、個別代入でも `t[i][j]()` が SIGSEGV。funcptr 配列の局所登録
  （decl.c:3185）が括弧内 `[N][M]` を inner_array_mul の積に潰し flat 1D 登録で多次元 stride 未設定。
  括弧内個別次元（g_inner_array_dims）を捕捉し outer_stride/mid_stride（要素 8B funcptr）を設定。stride が
  立つことで 2D 配列と正しく認識され brace init も通る（E3064 も解消）。2D/3D・brace init・個別代入・
  値→呼び出し・引数つきを網羅。1D funcptr 配列は不変。**これで 2D ポインタ配列（global/local × data/funcptr）
  を全て解消。**
  **注**: HANDOFF の「既知の差異」末尾の complex.h 欠如は解消済み（include/complex.h
  が存在）。`1.0i` 虚数サフィックス・`__real__` 等は GNU 拡張で対象外（clang は受理するが追わない）。

### このセッション（続き4）: C11 文字列リテラル & 欠落ヘッダ対応
「C11 仕様の網羅状況」を確認 → サブセットと判明。文字列リテラルと欠落ヘッダを順に対応:
- **UTF-16/UTF-32/wide 文字列リテラルの配列初期化** `unsigned short s[]=u"hi"`（9d9f7db）wide_string_literal_init。
  文字定数 `u'A'` は動くのに配列初期化子で壊れた (明示サイズ→値 0、`[]`→E3064、global→.comm 0)。
  3 経路（ローカル init / `[]` 推論 / global init）が要素幅 1 決め打ち。char_width と elem_size 一致時に
  各コード単位を要素幅で格納するよう一般化。ASCII 内容のみ。
- **欠落 C11 標準ヘッダ 10 個を同梱**（2307f83）c11_standard_headers。iso646/stdalign/stdnoreturn/uchar/
  inttypes/fenv/locale/wctype/wchar/tgmath（関数実体はシステム libc）。tgmath 対応で **4 件のコンパイラ
  バグも修正**: (1) `long double` が _Generic 関連型でパース不可（整数 cast-spec が `long` だけ食べる）。
  (2) `double` 制御式が `long double:` に誤マッチ（共に TK_DOUBLE）→is_long_double で区別。(3) 外部関数
  アドレス `fp=sqrt` が adrp @PAGE 直参照でリンク失敗→GOT 経由（@GOTPAGE）。(4) `_Generic(...)(args)` の
  bare funcref 呼び出しが間接化しシグネチャを失い fp 戻り値を x0 で読む→funcref callee を直接呼び出しに
  変換しプロトタイプ ABI を適用。E2E は category-binary 許可リストに wctype/wchar/fenv/locale 関数を追加。
  **C11 残ギャップ**: （下記「続き5」で解消）

### このセッション（続き5）: C11 残ギャップ 2 件を解消
- **非 ASCII の UTF-16/UTF-32 文字列リテラル**（95f095f）wide_string_literal_init 拡張。`U"aあb"` が UTF-8
  バイトをそのまま code unit 化し 6 要素に化けていた。tk_decode_utf8 + 幅対応 tk_next_string_code_units を
  追加し emit / 配列 init(local/global) / 要素数推論の 4 箇所を統一（char/u8=1byte、u=UTF-16 BMP1/補助面
  サロゲート対、U/L=UTF-32）。
- **_Generic で long double を double と区別**（8eaf519）generic_long_double。long long/plain char と同じ
  side-channel ビット (node_mem_t.is_long_double) を宣言時に立てノードへ伝播し infer_generic_control_type が
  読む。fp_kind は DOUBLE のままで codegen 不変、値は同一、_Generic 選択のみ変わる。ローカル変数・cast で
  機能（long long と同じ範囲）。**残既存制約**: 仮引数・グローバル変数は long long/plain char/long double
  すべてで _Generic 型区別ビットを伝播しない（params/globals に is_long_long 等が無い共通の既存ギャップ。
  long double 固有でないため今回は対象外）。

### このセッション末の網羅探索（~84 probe、すべて clang 一致 = 新規バグなし）
2D ポインタ配列を全象限修正した後、手薄だった領域を網羅探索したが miscompile はゼロ。
**再探索不要の領域は bug_coverage.md「チェック済みだが miscompile でなかった領域」末尾（2026-06-22 節）に
索引済み**: 複合代入・ビット幅・enum×bitfield・整数昇格/変換・ABI(>8引数/混在/スピル)・三項/論理・
ポインタ/配列境界・VLA/typedef/宣言子・集約初期化・グローバル初期化・文字列/文字エスケープ・除算剰余符号・
リテラル各種・プリプロセッサ stringize 等。式・制御フロー・ABI・数値変換は堅牢で、バグは依然「宣言子・型の
複雑な組合せ」に集中する傾向。

### 発見したが未修正（次セッションの着手候補）
- 現状、HANDOFF 列挙の既知未対応バグはすべて消化済み。上記網羅探索領域も再探索不要。再開時は
  **未探索の角度**（最適化が絡む大きめプログラム、複数 TU リンク、ライブラリ関数との相互作用、
  ランダム生成ファズ）から新規 miscompile を炙り出す。索引は `docs/differential_testing/bug_coverage.md`。

### このセッション中の注意（プロセス）
- ヘッダ（token.h / semantic_ctx.h / parser_public.h / node_utils.h 等）を変更すると **増分ビルドが
  依存を取りこぼし古い .o を使う**ことがあり、`make test` が偽の `test=2` を出した（2回）。
  ヘッダ変更時は `make clean && make` で確認すること（コードの問題ではない）。

## 2026-06-21 セッションの修正（bug_coverage.md の ⬜ 未着手をすべて消化）
現在 `make test` = **984 E2E + unit/parser/preprocess/fuzz/IR すべて green**、回帰 fixture は
`test/fixtures/probes_found_bugs/` 登録済み。`docs/differential_testing/bug_coverage.md` を
索引として更新済み。テーマ別:

- **ファイルスコープ static の内部リンケージ**（72b23c2）。`static` 変数/関数/関数内 static を
  `.global` で出し、無初期化 static を `.comm`(外部 common) にしていたため別 TU の同名 static と
  duplicate symbol 衝突/共有していた (C11 6.2.2p3 違反)。`is_static` を global_var_t / ir_func_t /
  node_func_t に追加し parser から伝播、codegen で `.global` 抑制 + 無初期化を `.zerofill __bss`。
  fixture: static_internal_linkage。**複数 TU の差分テストで発見**。
- **クロス TU (複数ファイル) E2E ケース種別を追加**（39e4f37）。`test_e2e.c` の `link2_cases[]` で
  2 ファイルを同じ接頭辞で namespace して category binary に一緒にリンクし、別 TU の同名シンボル
  衝突を検査できる。fixture: static_internal_linkage_xtu_{main,other}。
- **printf 浮動小数書式は green**（ab4355b、docs のみ）。`%f/%g/%e/%E`・精度/幅/フラグ・偶数丸め・
  Inf/NaN・float→double 可変長昇格・snprintf を stdout 比較。書式は libc 任せで ag_c は引数を
  正しく渡しており全一致。
- **`_Generic` の複雑な派生型照合**（5632b86 局所変数 / 1cbf0cb cast / cc1e788 グローバル変数）。
  関数ポインタの引数リスト (`int(*)(int)` vs `int(*)(int,int)`) や深いネスト型
  (`int(*(*)(void))[3]`) を generic_type_t の構造的フィールドでは区別できなかった。型を
  **正規化トークン文字列** (`psx_serialize_decl_type_tokens`) にして strcmp 照合する。control が
  局所変数のときは宣言時に名前で記録 (decl.c の副テーブル)、グローバルは永続表、cast は cast 経路
  で type_sig を採用。fixture: generic_complex_derived_type{,_global}。
- **ストリーミング前方先読み境界バグ**（1d584e8、既存バグ）。`_Generic` の型照合はカーソルを
  進めず `t->next` で型全体を先読みするが、ストリーミング生成器の materialize 窓を越えると
  `t->next==NULL` を踏み有効な型を誤却下 (E2006 ': 必要')。深い先読みの直前に
  `tk_ensure_lookahead()`（プリプロセッサが登録するフック経由で `pps_refill`、parser↔preprocess は
  疎結合維持）で窓を満たす。fixture: generic_streaming_lookahead。詳細は bug_coverage.md。

## 2026-06-18 セッションの修正（11 件、7e39081 まで）
現在 `make test` = **914 E2E + unit/parser/preprocess/fuzz/IR すべて green**、各修正は
ASAN クリーン、`test/fixtures/probes_found_bugs/` に回帰 fixture 登録済み。
- 符号付き short/char キャストの符号拡張（インライン使用で sign-extend、f9f153f）。
  fixture: cast_short_char_sign_extend。
- 2D 配列の行のポインタ算術 `*(m[i]+k)` / `*(p+k)+j`（node_is_ptr_for_arith で行を
  ポインタ判定、build_node_deref で 8B 以下の行も崩壊、1fd9eb1）。array_row_decay_pointer_arith。
- 3D 以上の中間行のポインタ算術 `*(*(t[i]+j)+k)`（多段ストライド伝播、47975d4）。
  array_row_decay_3d_pointer_arith。**変則 `*(t+1)[0]` 形は未対応（既存）**。
- 関数ポインタ変数の float/double 戻り値（callee の pointee_fp_kind を funcall へ、0b980b0）。
  funcptr_fp_return。**配列要素/struct メンバ/global の funcptr 戻り、int→fp 引数昇格は未対応**。
- static struct/union 局所の永続化（global lowering + stmt.c の static フラグ復元、8167e8e）。
  static_local_struct_persist。**インライン定義の匿名タグは未対応（codegen でタグ消失）**。
- i32 比較を 32bit 幅（w レジスタ）で行う + sub-int 戻り値の切り詰め（42b9d54）。
  `int f(int x){return x-1;} f(0)!=-1` 等の負値インライン比較を是正。戻り型 unsigned 追跡
  （ret_is_unsigned）と funcall への伝播も追加。int_cmp_width_and_subint_return。
- unsigned char/short 戻り値のゼロ拡張（前項の回帰修正、1b5e1df）。
- 匿名 struct/union メンバ昇格時の fp_kind/is_bool/is_unsigned/outer_stride 伝播（f4bf0bd）。
  `struct { union { int n; float f; }; }` の `s.f` 整数化を是正。anon_member_fp_unsigned_promote。
- グローバルポインタ配列 `&data[n]` / `data+n` 初期化（resolve_global_addr_init + codegen で
  `_sym+off`、138cd70）。global_ptr_array_addr_init。
- グローバル集約の `.member[idx]` / `.member.sub` designator チェーン（1e843b4）。
  global_designator_member_index。
- ローカル designator の struct leaf を `{...}` で brace 初期化（7e39081）。
  local_designator_aggregate_leaf。**union leaf は未対応（union 配列要素 brace init が別途未対応）**。

手法・再現手順は冒頭の「作業のやり方」を見ること。下記「既知の未対応」も更新済み。

## 作業のやり方（再現手順）
- 差分テストヘルパ（**正本は repo 追跡の `scripts/agc_diff_test.sh`**）:
  - `scripts/agc_diff_test.sh <file.c>` — repo ルート（スクリプト位置から自動導出、CWD 非依存）で
    ag_c を実行。`include/stdarg.h` 等の同梱ヘッダ (`#include`) を解決できる。src は `cd $ROOT`
    するので **絶対パス推奨**。`ag_c → .s → clang でアセンブル → 実行` と `clang -w -I include 直接`
    を実行し、**exit code・stdout・正規化した stderr の 3 つを比較**して
    `OK`/`MISMATCH`/`AGC_COMPILE_FAIL` を表示する。
  - **【重要】exit code だけ比較する罠**: assert 失敗はすべて exit 134 に潰れるため、
    exit code だけ比較すると ag_c と clang が**別々の assert で abort**しても両方 134 で「OK」と
    誤表示する。これを避けるため stdout と stderr も比較する。stderr は assert メッセージの
    `file <path>,` 部分だけ正規化 (ag_c=basename / clang=フルパスの差を吸収)、残りの式・関数・
    **行番号**を比較する。挙動が同じなら 3 つすべて一致するはず。MISMATCH 時は
    `[rc stdout stderr]` のどれが食い違ったかと差分を表示する。printf 系の stdout 差も同じツールで
    検出できる（別スクリプト不要）。
  - **クロス TU (複数ファイル)**: 各 .c を ag_c で .s 化 → clang で個別アセンブル → 一緒にリンク
    し、`clang -w -I include` の直接ビルドと比較する使い捨てスクリプトを `/tmp` に作って使う
    （src は絶対パスで渡すこと）。恒久回帰は `test_e2e.c` の `link2_cases[]`（2 ファイルを同接頭辞で
    namespace して category binary にリンク）に追加する。
  - 使い捨ての `/tmp/*.sh` を作るときは `Write` ツールで作る（`echo >` は使わない方針）。
- ag_c はアセンブリを **stdout** に出力する (`./build/ag_c foo.c > foo.s`)。`-o` や `-I` の
  コマンドラインフラグは無い。include 検索は CWD 相対の `include/`（preprocess.c の
  `k_include_search_roots`）。
- ビルド: `make`（日本語診断 `-DDIAG_LANG_JA`）。Edit 直後に mtime 粒度で再ビルドされない
  ことがあるので `touch <file>` してから `make`。
- 全テスト: `make test`（E2E + parser/preprocess 単体 + fuzz + IR）。E2E は
  `test/test_e2e.c` に登録、fixture は `test/fixtures/probes_found_bugs/`。
- **メモリ系バグは ASAN で特定**するのが有効だった（間欠 SIGSEGV を 1 回で再現）。
  zsh は単語分割しないので `${=SRCS}` を使う:
  ```
  clang -std=c11 -g -O0 -fsanitize=address -DDIAG_LANG_JA -Isrc \
    src/*.c src/config/*.c src/arch/*.c src/tokenizer/*.c src/parser/*.c \
    src/preprocess/*.c src/ir/*.c src/diag/diag.c src/diag/error_catalog.c \
    src/diag/ui_texts.c src/diag/warning_catalog.c src/diag/messages_ja.c -o /tmp/ag_c_asan
  ```
  注意: グロブ (`src/*.c`) は **コマンドラインに直書き** で展開する。`SRCS="src/*.c ..."` を
  `${=SRCS}` で渡すとグロブ展開されず `no such file` になる（単語分割しかしないため）。
  messages_en.c / messages_all.c は messages_ja.c と重複定義になるので入れない。
- 新規 fixture の登録は `test/test_e2e.c` に 2 系統:
  - `CASE_INT_FILE`（exit code で判定、慣習で 42）:
    `{"probes", "<name>", CASE_INT_FILE, "...<name>.c", 42, 0}`。
  - `CASE_ASSERT_FILE`（fixture 内の `assert()` が期待を自己記述、成功で main が 0 を返す。今回の
    `_Generic`/linkage fixture はこちら）: `{"probes", "<name>", CASE_ASSERT_FILE, "...<name>.c", 0, 0}`。
    `#include <assert.h>` と `int main(void){ assert(...); return 0; }` 形式。
  - 回帰検証は「修正を一時的に外して fixture が落ちる」ことまで確認する（バグ注入 → make / 該当
    fixture 実行で失敗を確認 → 復元）。`touch` してから `make`（mtime 粒度の取りこぼし回避）。
- **【重要】探索が green だった領域も必ず記録する**。差分テストして miscompile が無かった（clang 一致）
  領域は、`docs/differential_testing/bug_coverage.md` の「チェック済みだが miscompile でなかった領域
  （再探索不要）」にテーマ別で追記する（日付節を作る）。記録しないと次セッションが同じ領域を再探索して
  しまう。バグ修正（🔧）だけでなく「探索したが堅牢だった」ことも成果として残すこと。

## アーキテクチャ早見
tokenizer → preprocess → parser（`src/parser/`）→ IR builder（`src/ir/ir_builder.c`）→
ARM64 codegen（`src/arch/arm64_apple*.c`）。ターゲットは Apple Silicon ARM64。

- ノード型情報のアクセサ: `src/parser/node_utils.c`（`psx_node_type_size` /
  `psx_node_is_pointer` / `psx_node_is_unsigned` / `psx_node_pointer_qual_levels` /
  `psx_node_base_deref_size` / `psx_node_set_unsigned` など）。`node->kind` で分岐し、
  ND_LVAR は `mem`、ND_GVAR/DEREF/ASSIGN は `node_mem_t`、その他は base を読む（フィールド
  が分散しているので **読み書きでフィールドを一致させること**）。
- struct メンバ情報は `tag_member_t`（semantic_ctx.c）/ `tag_member_info_t`（header）。
  メンバ属性（fp_kind / is_bool / is_unsigned / outer_stride）は setter/getter を介して
  hash table に保存し、`psx_ctx_get/find_tag_member_info` で取得。新属性を足すときは
  ① 両構造体にフィールド ② setter/getter ③ struct_layout.c で設定 ④ build_member_deref_node
  で deref ノードへ伝播、の 4 箇所を揃える（`is_bool` を雛形にできる）。
- struct 値の ABI（簡略版）: 引数は**ポインタ渡し**（callee が memcpy）、>16B 戻り値は
  x8 経由の ret_area。`build_node_funcall` は値文脈で >8B struct 戻り値の ret_area を確保
  しアドレスを返す。
- グローバル初期化は `psx_parse_global_brace_init_flat`（parser.c）が **フラット index** に
  値を書き、`emit_global_struct_init`（arm64_apple.c）が struct レイアウトに沿って出力する
  （入れ子 struct は再帰展開）。

## 直近セッションの要約（db98d34〜e0b5190、22 件）
現在 `make test` = **904 E2E + unit/parser/preprocess/fuzz/IR すべて green**、各修正は ASAN
クリーン、`test/fixtures/probes_found_bugs/` に回帰 fixture 登録済み。テーマ別の内訳:
- 型幅/キャスト: sub-int の (int)/(unsigned) 符号（1317698）、(int)/(long) の 32/64bit
  切り詰め・拡張（1c8e358, 6070c7e, 8ddafa6）、long リテラルの i64 型付け（365d8c0）、
  インライン `*(int*)ptr` の pointee サイズ（d44947f）
- ポインタ/配列: struct ポインタ配列メンバ（db98d34）、多次元配列の行 decay（a2a8328）、
  struct/union ポインタ算術スケール（b6a42ec）、配列へのポインタ（>4B struct, 局所2D）
  （10072cc, 5a47279）
- 集約: ネスト designator の `[idx]`（aadf3b7）、struct 配列メンバ brace init（1684a8c）、
  _Bool 初期化子正規化（5b3d592）
- struct ABI: 3/5/6/7B struct の値渡し/返し（050e1bf）
- **float/double**（codegen の穴を集中的に）: 配列メンバ access/init（10b9748）、
  多次元 subscript の fp load（10d291d）、ブール条件分岐（ec96e30）、`&&`/`||`（87a24a1）、
  static ローカル float init（f873777）
- 型機能: `_Alignof` の集約アラインメント（3e8a4d1）、`_Generic` の文字列/long リテラル（e0b5190）

詳細は下記テーマ別セクション参照。手法・再現手順は冒頭の「作業のやり方」を見ること。

## このセッションで修正した内容（コミット c1a8f83 以降）
テーマごと。すべて `make test` green、該当パスは ASAN クリーン、fixture 登録済み。

### 64bit 幅 / long
- ポインタ三項の null 定数分岐が 4 バイトしか書かず garbage（d150e3b）
- ternary / binop / リテラル / 戻り値 / 引数の 64bit 幅取りこぼし（4329e49, 56e96fe,
  a9a965c, fcbd1e4）— long の値が下位 32bit に化ける一連
- 入れ子三項の long 分岐（fcbd1e4 に含む）

### ポインタ / subscript（`build_subscript_deref` 周辺の「配列 vs 多段ポインタ」取り違え）
- struct ポインタ局所変数の type_size が 8 でなく pointee サイズで、`p=p->next` が 16B
  struct コピー扱いになり隣接スタック破壊（f0b096c）
- 単段ポインタ subscript 結果がスカラなのに pointer 扱い（6fe7dce）
- 多段ポインタ subscript の pql 減算（d0ff96c）/ base_deref_size 伝播（8e937e0）
- `long *a` 仮引数 subscript / 呼び出し側 long 戻り値幅（a9a965c）

### 集約初期化（C11 6.7.9）— ローカル & グローバル
- 多次元配列メンバ `int a[2][2]`（4b92768）
- 重複 designator 後勝ち（4a5942d、**ユーザー確認のうえ C11 準拠を選択**）
- designator 後の位置指定継続（df23e17）
- ネスト struct メンバ brace 省略 `{1,2,3}`（17edc06）
- struct 配列フラット展開 / 配列メンバ部分充填→designator（b225c4c）
- グローバル入れ子 struct 初期化の data 出力（5be6940）
- グローバル designator の slot 計算（入れ子 struct / struct 配列 `[N]=`）（1b79768）
- 関数ポインタ配列 struct メンバの brace 初期化（7e6912c）
- **struct 配列メンバ** `struct P pts[N]` の要素 brace 初期化（1684a8c）。ローカルは
  parse_member_initializer が要素を parse_struct_initializer へ委譲、グローバルは
  emit_global_struct_members_rec が各要素 struct を再帰展開。positional/designated/
  部分ゼロfill 対応。差分テストで発見。**未対応**: グローバルでネストメンバ内の配列添字
  designator `{.items={[2]={.a=7}}}` はグローバル flat パーサの別制約（h83k）。
- メンバパス途中に配列添字を含む designator `.m.x[1].b` / `.arr[i].f`（aadf3b7）。
  `consume_nested_designator_and_build_assign` を `.member` と `[idx]` の任意連鎖を
  辿るよう一般化し、indexed 専用ヘルパを統合（scalar/union/配列要素・境界チェック維持）。

### struct ポインタ配列メンバ
- `struct N *arr[N]` メンバの subscript 結果が struct ポインタと認識されず
  `h.arr[i]->v` が E3005（db98d34）。`build_member_deref_node` がポインタ配列メンバに
  pql=1 / base_deref_size=要素 pointee サイズ を立て、`build_subscript_deref` の
  「要素がポインタ」分岐に乗せて is_tag_pointer を伝播（ローカル `T *arr[N]` と同じ表現）。

### unsigned 符号拡張（load / 比較の signedness）— **全経路を網羅**
- `(int)`/`(unsigned)` キャストが operand の符号を更新せず比較が誤符号（5b474f7）。
  ※ binop ノード（シフト）の is_unsigned は LSR/ASR を兼ねるので終端値ノードのみ更新。
- unsigned グローバル / struct・union メンバ（70d91ab）、long/char/short 幅（3a61e09）、
  配列要素 / ポインタ deref（14ca899）、typedef したグローバル（8bd7ec8）
- **char/short を (int)/(signed)/(unsigned) へ明示キャスト**すると operand の
  is_unsigned 上書きが load 拡張 (ldrsh/ldrh) を変え値が化けていた（1317698）。
  `(unsigned)(short)-1`→65535、`(int)(unsigned short)0xFFFF`→-1。sub-int は load
  符号性を保ち、(unsigned) は & 0xffffffff で 32bit unsigned へ折り返す（符号混在の
  インライン比較/除算も是正）。差分テストで発見。

### 多次元配列の decay（行→ポインタ）
- 多次元配列の途中次元（行）を**値**として使う（`int *q=m[0]`、`*(*(m+1)+2)`、`**m`）と
  int* へ崩壊せず値ロードして garbage（a2a8328）。① build_node_deref の崩壊判定が
  is_pointer=1 を要求していたのを `deref_size>0 && type_size>8` に緩和（struct 値は
  deref_size=0 で除外）。② 通常多次元配列は ND_ADDR(deref_size=行, inner_deref_size=要素)
  表現で、build_unary_deref_node が `*m`/`*(m+k)` の内側ストライドを引き継いでいなかった。
  ND_ADD を辿り 1 段シフト。3D・関数引数 `int(*)[N]`・グローバル 2D も対応。差分テストで発見。

### _Bool 初期化子の正規化
- `_Bool` は 0/1 を保持すべき（C11 6.3.1.2、非ゼロ→1）だが、スカラ変数 init のみ正規化され
  配列・struct メンバ・グローバルの各 init 経路は生値を格納（`_Bool f[]={5}` が 5）（5b3d592）。
  全 init サイトで (value!=0) を適用: ローカル配列 / ローカル struct スカラ・配列メンバ /
  グローバル配列（global_var_t に elem_is_bool 追加）/ グローバル struct メンバ（emit で
  tag member is_bool 参照）。`bool_normalize_if` ヘルパ。差分テストで発見。

### (int)/(signed)/(unsigned) による long の 32bit 切り詰め
- `(int)long` が 32bit に切り詰められず、代入/戻り値では store 幅で偶然合うがインライン比較/
  演算で 64bit のまま使われ `(int)0x100000000L == 0` や `(int)long_var == 0` が偽（1c8e358）。
  定数は値を切り詰め、long 値は `(x<<32)>>32`（signed=算術 / unsigned=論理シフト）で低32bitを
  64bitへ拡張。sub-int・ポインタ→int は対象外。差分テストで発見。

### _Generic の制御式型推定（文字列・long リテラル）
- `_Generic("str", char*:...)` が文字列を char* に decay 認識せず（ptr_deref_size 未設定）default に、
  `_Generic(42L, long:...)` が long リテラルを int 扱い（scalar_size=4）で long にマッチせず（e0b5190）。
  文字列は ptr_deref_size=文字幅、NUM は int_is_long で scalar_size=8 に。差分テストで発見。

### _Alignof が集約型でサイズを返す
- `_Alignof` が sizeof 実装で、基本型は size==align で偶然合うが struct/union/配列で誤り
  （`_Alignof(struct{int,int})`=8（正4）、`_Alignof(int[10])`=40（正4）、`_Alignas(16)` 含む
  struct=32（正16））（3e8a4d1）。tag テーブルに agg_align を保存（layout が pending 経由で
  define に渡す）。_Alignof モードでは struct の align を返し、配列は要素数を掛けない。
  sizeof は不変。旧挙動を符号化していた E2E fixture 2 件・parser 単体 2 件も修正。差分テストで発見。

### static ローカル float/double の初期化
- `static float t = 0.5f;` が `.long 0` で出力され値が 0 になっていた（f873777）。
  try_lower_static_local_scalar が NUM の整数 `->val` のみ読み、float リテラル値 `->fval` を
  無視していた。fp なら fval/fp_kind を gv に伝播。差分テストで発見。

### float をブール条件/論理演算で使う（codegen）
- `f ? a : b` / `if(f)` / `while(f)` / `f && g` / `f || g` で float/double を真偽値として使うと、
  レジスタ圧で fp が spill されたとき codegen が 4B float を 8B 整数 load して上位 garbage を拾い、
  0.0 が真と誤判定（ec96e30 で条件分岐、87a24a1 で `&&`/`||`）。`emit_truthiness` ヘルパ（fp は
  `!= 0.0` の IR_FNE、整数は IR_NE）に統一し、emit_br_cond と build_node_logand_or 右辺で使用。
  差分テストで発見。

### 多次元 float/double 配列の subscript
- `float m[2][3]` の `m[i][j]` が整数 load で読まれていた（10d291d）。build_subscript_deref が
  float 配列 subscript 結果の fp_kind を常に base.fp_kind に載せていたため、多次元 1 段目（まだ行）
  の結果が float 値扱いになり、次段 subscript が pointee の fp 種別を失い整数 load。中間（行）は
  pointee_fp_kind、最終要素のみ base.fp_kind に（is_bool と同じ分岐）。`es > inner_ds` で多次元中間
  かを判定（`float *a` 仮引数は inner_ds=elem が立つので es 比較で区別）。差分テストで発見。

### struct の float/double 配列メンバ
- `struct S{ float v[N]; }` の `s.v[i]` アクセスと brace 初期化が整数扱いで値が化けた（10b9748）。
  アクセス: build_member_deref_node が配列メンバの fp_kind を base.fp_kind に入れていたのを
  pointee_fp_kind に（is_bool と同じ分岐、subscript 結果を fp load）。初期化: parse_member_initializer
  に member_fp_kind を通し要素 store を fp store に（build_member_array_elem_assign_node）。差分テストで発見。

### 局所「2次元配列へのポインタ」の mid_stride
- 局所 `T (*p)[N][M]` が mid_stride 未設定で `p[i][j]` が誤スケール（5a47279）。局所宣言経路は
  paren の `[N][M]` を積(outer_stride)としてのみ扱い第2 subscript 用 stride を立てていなかった。
  paren-array の先頭次元・次元数を捕捉し mid_stride=(積/先頭次元)*elem を設定。int/struct 共通。
  引数版は元から動作。差分テストで発見。

### 配列へのポインタ（要素が >4B struct）
- `struct T (*p)[N]` の subscript が要素 struct >4B のとき壊れた（10072cc）。局所は
  lvar_is_pointer が `size>elem_size`（8>8 が偽）でポインタ非認識（4B struct は 8>4 で動作）、
  引数は struct ポインタ仮引数ブランチが `[N]` を無視し `struct T *` 扱い（行を跨げない）。
  局所は `outer_stride>0 && size==8` でポインタ認識、引数は is_tag_pointer クリア＋outer_stride
  設定。`long (*g)[N]` 引数の pql=1 化も抑止。差分テストで発見。

### long/long long リテラルの 64bit 型付け
- 値が 32bit に収まる long サフィックス付きリテラル（`2L` 等）が i32 扱いになり、`u * 2L`
  （u は unsigned >2^31）が 32bit 演算で wrap（365d8c0）。tokenizer は int_size に L を記録
  していたが parser が node に伝えていなかった。node_num_t に `int_is_long` を追加し
  parse_num_literal で伝播、build_node_num が long リテラルを i64 生成。is_unsigned サフィックスも
  node へ伝播。通常 int リテラルは値ベース型付けのまま（`w * 2u` の wrap は不変）。差分テストで発見。

### (long)unsigned_int の zero-extend（arithmetic）
- `(long)u + (long)u`（u は unsigned int で値 >2^31、合計が 2^32 超）が 32bit でラップ（8ddafa6）。
  `(long)` が type_size を 8 に広げない no-op のため二項演算 result_ty が I32 になり、符号なし
  2^32 ラップマスク（[ir_builder.c]~1230）で切り詰められていた。`coerce_to_type` は常に SEXT で
  unsigned widen に乗れないので、node_mem_t に `widen_zext_i64` フラグを追加し、`(long)unsigned`
  (8B未満) を ND_PTR_CAST(widen_zext_i64) でラップ → `build_node_ptr_cast` が IR_ZEXT を明示挿入。
  signed の (long) は SEXT で従来通り。差分テストで発見。

### インラインのポインタキャスト deref
- `*(int*)(cp+4)`（char* を別要素型へキャストして即 deref/添字）が、キャストで pointee
  サイズを更新せず元の char サイズ(1)でロードしていた（d44947f）。一旦変数 `int *p=...` に
  入れると正しく動いた。スカラ整数型への単段ポインタキャストを ND_PTR_CAST で deref_size
  更新。operand がポインタのときのみ（`(int*)0` 等の null 定数は ND_NUM のまま）、多段は除外。
  差分テストで発見。

### 小さい struct の値渡し/値返し ABI（サイズ 3/5/6/7）
- サイズが 1/2/4/8 でない struct/union（3/5/6/7B）を値渡し/値返しすると、先頭メンバ幅の
  スカラとして 1 レジスタで運ばれ先頭メンバしか残らなかった（`{char;short;uchar}` 6B が
  1B 扱い）（050e1bf）。簡略 ABI が至る所で `>8` を間接（memcpy/x8 ret_area）境界にしていた
  のが原因。`cg_size_needs_indirect_struct`（1/2/4/8 以外を間接）を追加し、引数渡し/受け取り・
  struct 代入・戻り値設定/受け取り・struct 戻り値呼出の内側 arg ループ・複合リテラル引数の
  全経路に適用。1/2/4/8B は従来通りレジスタ値渡し。差分テストで発見。

### struct/union ポインタ算術
- `&s[i]-&s[j]` / `sp+n` / `n+sp` / `sp-n` が要素サイズで割られず/掛けられず byte 単位
  だった（b6a42ec）。struct タグポインタは is_pointer でなく is_tag_pointer で表現され、
  `add()` の `psx_node_is_pointer` 判定が偽を返していた。`node_is_ptr_for_arith`
  （タグポインタも認識）を追加。複合 `+=`/`-=` は別経路で元から正常。差分テストで発見。

### long bitfield
- `unsigned long a:40` 等 >32bit / 32bit ユニットを跨ぐ bitfield（040da11）。storage を
  8 バイト許可 + 64bit load/store + マスク定数をレジスタ展開。

## 既知の未対応 / 次の着手候補
（差分テストで新しい miscompile / コンパイルエラーを炙り出すところから再開する。
2026-06-18 セッションで上記 11 件、2026-06-21 セッションでさらに数件を修正済み。下記は現時点で
残る既知の未対応。直近のプローブ（型×文脈の総当たり寄り）はかなり green で、発見コストが
上がっている。）

- **現状の索引は `docs/differential_testing/bug_coverage.md`**。チェック済み領域・⚠️/🔧 状態・
  既存バグ・「miscompile でなかった」領域をテーマ別に集約している。2026-06-21 時点で同ファイルの
  ⬜ 未着手候補（複数 TU/extern、printf 書式、`_Generic` 複雑派生型）は全消化。再開時はまず
  これを見て未探索の「型 × 宣言経路 × 使用文脈」の組合せを選ぶ。下記は古い既知の未対応の抜粋。

- **関数ポインタ FP 戻り値の残り**（funcptr 変数は 0b980b0 で対応済み）。callee が配列要素
  `ops[i]` / struct メンバ `s.f` / グローバル関数ポインタの場合は戻り型 fp_kind が funcall へ
  伝播せず戻り値を x0 で読む。subscript/メンバ/global_var_t への配線が要る。また間接呼び出しの
  **int→fp 引数昇格**（`double(*p)(double); p(4)` の整数リテラル）は仮引数型保存が必要で未対応。

- **codegen の符号性/幅の深い穴**。`unsigned long` / `unsigned char` 戻りの符号性は plain
  `unsigned` のみ追跡（ret_token_kind が TK_LONG/TK_CHAR に潰れる）。混在幅の比較で片側が
  i32・片側 i64 のケースは 64bit 比較になる（gen_inst_int_cmp は両 i32 のときだけ 32bit）。

- **多次元・ポインタの変則形**。`*(t+1)[0]`（pointer-to-2D-array に subscript+deref 混在）は
  コミット前から SIGSEGV（47975d4 の標準 3D 行算術とは別経路）。

- **union 集約初期化の穴**。`union U arr[2]={[1]={.n=5}}` / `.u[1]={...}`（union 配列要素の
  brace init）が誤値。local designator の union leaf は 7e39081 で意図的に E3064 のまま除外。

- グローバルのネスト brace 配列添字 `struct O o={.items={[2]={.a=7}}}`（`{[2]=...}` 形）は
  グローバル flat パーサの制約で E3064。`.items[2].a=` 形（designator チェーン）は 1e843b4 で対応済み。

- 同梱ヘッダに `complex.h` が無く `#include <complex.h>` が E1034。`_Complex` の言語機能自体は
  動く（creal/cimag 等のライブラリ関数を使わなければ可）。

## バグではない（仕様 / 既知の差異、追わない）
- statement expression `({...})`（GNU 拡張、非標準。プロジェクトは C11 準拠）。
- 過剰初期化子 `struct S s={{1,2},{3,4}}`（メンバ1個に2グループ）等は clang が警告して
  無視するが ag_c は E3064。意図的に厳格（miscompile ではない）。
- `s07.c`（深さ 10 万の再帰）が SIGSEGV。各フレームのスタック使用量が大きいことによる
  スタックオーバーフローで、誤コンパイルではない（深さ 100 では正しく動く）。
- 評価順序が未規定/UB のもの（`a[i++]=i` 等）。

## メモ
- 「型バリエーション × 宣言経路 × 使用文脈」の組み合わせで取りこぼしが多発する傾向。
  例: unsigned を直しても int だけ／local だけ／scalar だけ、になりがち。1 つ直したら
  long/char/short・global・typedef・配列要素・ポインタ deref まで広げて確認すると良い。
- 集約初期化・struct 値受け渡しは「ノード種別を特別扱いする分岐」が多く、新しいノード形
  （compound literal=ND_COMMA、subscript=ND_DEREF、funcall 戻り値=ND_FUNCALL）が漏れがち。
