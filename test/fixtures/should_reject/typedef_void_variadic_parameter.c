/* A typedef for void cannot serve as a named parameter before an ellipsis. */
typedef void no_parameters;

int function(no_parameters, ...);

int main(void) {
  return 0;
}
