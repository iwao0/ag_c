typedef char classifier_name;

int main(void) {
  enum {
    classifier_name = sizeof(classifier_name),
    following_value = sizeof(classifier_name)
  };

  if (classifier_name != sizeof(char)) return 1;
  return following_value != sizeof(int);
}
