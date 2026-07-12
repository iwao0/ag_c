int main(void) {
  int selected = 1;
  int other = 2;
  int side_effect = 0;
  int control_effect = 0;

  _Generic(selected, int: selected, default: other) = 7;
  int value = _Generic((control_effect++, selected), int: 11,
                       default: (side_effect = 9));

  return selected == 7 && other == 2 && side_effect == 0 &&
                 control_effect == 0 && value == 11
             ? 0
             : 1;
}
