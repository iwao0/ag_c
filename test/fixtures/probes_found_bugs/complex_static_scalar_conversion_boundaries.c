// Complex-to-scalar conversions and equality in static initializers.
// Expected: exit=0
#include <complex.h>
#include <math.h>

static _Bool imaginary_truth = CMPLX(0.0, 2.0);
static _Bool zero_truth = CMPLX(0.0, 0.0);
static _Bool explicit_imaginary_truth =
    (_Bool)CMPLX(0.0, 3.0);
static double double_real = (double)CMPLX(3.0, -4.0);
static float float_real = (float)CMPLXF(5.0f, -6.0f);
static long double long_double_real =
    (long double)CMPLXL(7.0L, -8.0L);
static double negative_zero_real =
    (double)CMPLX(-0.0, 8.5);
static double implicit_double_real = CMPLX(13.0, -14.0);
static int integer_real = (int)CMPLX(15.75, -16.0);
static unsigned char unsigned_char_real =
    (unsigned char)CMPLXF(255.0f, 17.0f);
static int equal_value =
    CMPLX(9.0, -10.0) == CMPLX(9.0, -10.0);
static int unequal_value =
    CMPLX(11.0, -12.0) != CMPLX(11.0, 12.0);
static int scalar_equal_value =
    CMPLX(18.0, 0.0) == 18.0;
static int reverse_scalar_equal_value =
    18.0 == CMPLX(18.0, 0.0);
static int logical_not_zero = !CMPLX(0.0, 0.0);
static int logical_and_value =
    CMPLX(0.0, 1.0) && CMPLX(2.0, 0.0);
static int logical_or_value =
    CMPLX(0.0, 0.0) || CMPLX(0.0, 3.0);
static int selected_value =
    CMPLX(0.0, 29.0) ? 30 : 31;

struct converted_values {
    _Bool truth;
    double real;
    int equal;
};

static struct converted_values aggregate_values = {
    CMPLX(0.0, 19.0),
    (double)CMPLX(20.0, -21.0),
    CMPLX(22.0, 23.0) != CMPLX(22.0, -23.0),
};

static int check_block_static(void) {
    static _Bool truth = CMPLXF(0.0f, 24.0f);
    static float real = (float)CMPLXF(25.0f, -26.0f);
    static int equal =
        CMPLXL(27.0L, -28.0L) == CMPLXL(27.0L, -28.0L);
    return truth == 1 && real == 25.0f && equal == 1;
}

int main(void) {
    return !(imaginary_truth == 1 &&
             zero_truth == 0 &&
             explicit_imaginary_truth == 1 &&
             double_real == 3.0 &&
             float_real == 5.0f &&
             long_double_real == 7.0L &&
             signbit(negative_zero_real) &&
             implicit_double_real == 13.0 &&
             integer_real == 15 &&
             unsigned_char_real == 255 &&
             equal_value == 1 &&
             unequal_value == 1 &&
             scalar_equal_value == 1 &&
             reverse_scalar_equal_value == 1 &&
             logical_not_zero == 1 &&
             logical_and_value == 1 &&
             logical_or_value == 1 &&
             selected_value == 30 &&
             aggregate_values.truth == 1 &&
             aggregate_values.real == 20.0 &&
             aggregate_values.equal == 1 &&
             check_block_static());
}
