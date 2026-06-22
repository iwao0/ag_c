#ifndef _WCTYPE_H
#define _WCTYPE_H
/* C11 7.30: ワイド文字分類。wint_t は wchar_t を含める int。 */
#ifndef _WINT_T
#define _WINT_T
typedef int wint_t;
#endif
typedef int wctype_t;
typedef int wctrans_t;
#ifndef WEOF
#define WEOF ((wint_t)-1)
#endif

int iswalnum(wint_t wc);
int iswalpha(wint_t wc);
int iswblank(wint_t wc);
int iswcntrl(wint_t wc);
int iswdigit(wint_t wc);
int iswgraph(wint_t wc);
int iswlower(wint_t wc);
int iswprint(wint_t wc);
int iswpunct(wint_t wc);
int iswspace(wint_t wc);
int iswupper(wint_t wc);
int iswxdigit(wint_t wc);
int iswctype(wint_t wc, wctype_t desc);
wctype_t wctype(const char *property);
wint_t towlower(wint_t wc);
wint_t towupper(wint_t wc);
wint_t towctrans(wint_t wc, wctrans_t desc);
wctrans_t wctrans(const char *property);
#endif
