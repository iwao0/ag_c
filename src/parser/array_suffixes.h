#ifndef PARSER_ARRAY_SUFFIXES_H
#define PARSER_ARRAY_SUFFIXES_H

int psx_parse_array_size_constexpr(void);
int psx_parse_array_size_optional_constexpr(int *out_has_size);
int psx_parse_array_suffixes_constexpr_required(int base_mul);
int psx_parse_member_array_suffixes(int *out_is_flex_array,
                                    int *out_dim_count, int *out_first_dim);

#endif
