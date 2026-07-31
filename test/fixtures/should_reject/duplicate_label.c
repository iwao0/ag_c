/* A label name can be defined only once within a function. */
int main(void) {
label:
  goto label;
label:
  return 0;
}
