/* An array designator index must be within the known array bound. */
int values[3] = {[3] = 7};

int main(void) {
  return values[0];
}
