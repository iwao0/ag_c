static const int index_value = 1;
int values[3] = {[index_value] = 7};

int main(void) {
  return values[1];
}
