#ifndef PARSER_ARRAY_SUFFIXES_H
#define PARSER_ARRAY_SUFFIXES_H

int psx_parse_array_size_constexpr(void);
int psx_parse_array_size_optional_constexpr(int *out_has_size);
int psx_parse_array_suffixes_constexpr_required(int base_mul);
/* 上記と同じく積を返すが、各次元のサイズを out_dims[0..] に、次元数を out_dim_count に
 * 書き出す。括弧内配列 `(*t[2][2])` の多次元ストライド設定に使う。 */
int psx_parse_array_suffixes_capture_dims(int base_mul, int *out_dims, int max_dims,
                                          int *out_dim_count);
int psx_parse_member_array_suffixes(int *out_is_flex_array,
                                    int *out_dim_count, int *out_first_dim);

#endif
