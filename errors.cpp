#include "errors.hpp"

void printError(char const *errortext, int errorcode) {
  std::cerr << "Error: " << errortext;
  if (errorcode != 0)
    std::cerr << ": " << strerror(errorcode);
  std::cerr << "\n";
}
char const *strend(char const *str) { return str + strlen(str); }
