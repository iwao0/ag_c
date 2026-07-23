// A typedef name does not make `restrict` valid for a non-pointer type.
typedef int scalar_type;

int main(void) {
  return sizeof(restrict scalar_type);
}
