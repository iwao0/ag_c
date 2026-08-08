typedef char boundary_name;

int main(void) {
  boundary_name storage[sizeof(boundary_name)] = {'A'};
  boundary_name (*boundary_name)[sizeof(boundary_name)] = &storage;

  if (sizeof(*boundary_name) != sizeof(char)) return 1;
  if (sizeof(boundary_name) != sizeof(char *)) return 2;
  return (*boundary_name)[0] != 'A';
}
