#include <pthread.h>
#include <sys/stat.h>

#include <fstream>
#include <iostream>
#include <vector>

#include "errors.hpp"
#include "threads.hpp"

void usage(int argc, char const* argv[]) {
  std::cerr << "Usage:\n\t" << argv[0]
            << " <file_to_process> <number_of_threads> <character_to_count>\n";
  exit(1);
}

int main(int argc, char const* argv[]) {
  if (argc < 4) {
    usage(argc, argv);
  }
  size_t processorsQuantity;
  {
    char* end = nullptr;
    processorsQuantity = std::strtoul(argv[2], &end, 10);
    if (strend(argv[2]) != end || processorsQuantity < 1) {
      printError("Invalid argument value for number_of_processes", 0);
      usage(argc, argv);
    }
  }
  if (strlen(argv[3]) != 1) {
    printError("Invalid argument value for character_to_count", 0);
    usage(argc, argv);
  }
  off_t fileSize;
  {
    struct stat st;
    if (stat(argv[1], &st) == -1) {
      printError("Invalid file provided", errno);
      usage(argc, argv);
    }
    fileSize = st.st_size;
    if (fileSize < 2) {
      printError("Invalid file contents: too little symbols in file", 0);
      usage(argc, argv);
    }
  }
  if (processorsQuantity > (fileSize >> 2)) {
    std::cout
        << "Quantity of processes you entered (" << processorsQuantity
        << ") exceeds half of the amount of data (" << (fileSize >> 2)
        << ") to be processed. Actual number of processes will be reduced.\n";
    processorsQuantity = fileSize >> 2;
  }
  size_t blockSize = fileSize / processorsQuantity;
  size_t lastBlockSize = fileSize - blockSize * (processorsQuantity - 1);
  char character_to_count = argv[3][0];
  std::ifstream file{argv[1]};
  Mutex<size_t> result{0};

  auto p = [result](std::vector<char> data, char to_find) mutable {
    size_t res = 0;
    for (char ch : data) {
      if (ch == to_find) ++res;
    }
    result.lock() += res;
  };
  using FnPtr = decltype(p)&;
  std::vector<Thread> threads;
  for (size_t i = 0; i < processorsQuantity - 1; ++i) {
    std::vector<char> buf(blockSize);
    file.read(buf.data(), blockSize);
    threads.push_back(Thread(p, buf, character_to_count));
  }
  std::vector<char> buf(lastBlockSize);
  file.read(buf.data(), lastBlockSize);
  threads.push_back(Thread(p, buf, character_to_count));
  for (auto& th : threads) th.join();

  std::cout << result.lock();

  return 0;
}
