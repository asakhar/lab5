#include "shared_memory.hpp"
#include <cstdlib>
#include "errors.hpp"
#include <fstream>
#include <ios>

int main(int argc, char *argv[], char * /*env*/[]) {
  if (argc < 7) {
    _exit(1);
  }
  auto fileName = argv[1];
  int fd;
  {
    char *end = nullptr;
    fd = std::strtol(argv[2], &end, 10);
    if(end != strend(argv[2]) || fd < 3) {
      _exit(1);
    }
  }
  size_t blockSize;
  {
    char *end = nullptr;
    blockSize = std::strtoul(argv[3], &end, 10);
    if(end != strend(argv[3]) && blockSize < 1) {
      _exit(1);
    }
  }
  size_t offset;
  {
    char *end = nullptr;
    offset = std::strtoul(argv[4], &end, 10);
    if(end != strend(argv[4])) {
      _exit(1);
    }
  }
  size_t idx;
  {
    char *end = nullptr;
    idx = std::strtoul(argv[5], &end, 10);
    if(end != strend(argv[5]) && blockSize < 1) {
      _exit(1);
    }
  }
  if(strlen(argv[6]) != 1)
    _exit(1);
  char searchingFor = argv[6][0];
  SharedMem<size_t> sharedMem(fd);
  std::ifstream file{fileName};
  file.seekg(offset, std::ios_base::beg);
  for(size_t i = 0; i < blockSize; ++i) {
    auto character = file.get();
    if(character == searchingFor)
      ++sharedMem[idx];
  }
  _exit(0);
  return 0;
}