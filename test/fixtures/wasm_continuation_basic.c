int game_running(void);

static int observed;

int main(void) {
  int frame = 0;
  while (game_running()) {
    frame += 1;
    observed = frame;
  }
  observed += 100;
  return frame;
}

int read_observed(void) {
  return observed;
}
