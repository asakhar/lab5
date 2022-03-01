#include "errors.hpp"
#include "shared_memory.hpp"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <sched.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <vector>

constexpr char const *processorPath = "./processor";

#define SV(str) str, strlen(str)
// template<typename T>
// std::string toString(){std::stringstream ss; ss << val; }

void usage(int argc, char *argv[]) {
  std::cerr << "Usage:\n\t" << argv[0]
            << " file_to_process number_of_processes character_to_count\n";
  exit(1);
}
pid_t createProcess(char const *argv[]) {
  auto pid = fork();
  if (pid != 0)
    return pid;
  if (execve(argv[0], const_cast<char *const *>(argv), nullptr) == -1) {
    printError("Execve call resulted in error", errno);
    exit(1);
  }
  return pid;
}

int main(int argc, char *argv[], char * /*env*/[]) {
  if (argc < 4) {
    usage(argc, argv);
  }
  size_t processorsQuantity;
  {
    char *end = nullptr;
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
  auto blockSizeString = std::to_string(blockSize);
  auto sharedMem = SharedMem<size_t>(processorsQuantity);
  auto fdString = std::to_string(sharedMem.getFd());
  auto charString = argv[3];
  std::vector<pid_t> processes(processorsQuantity);
  std::array<char const *, 8> childArgv;
  childArgv[0] = processorPath;
  childArgv[1] = argv[1];
  childArgv[2] = fdString.c_str();
  childArgv[3] = blockSizeString.c_str();
  childArgv[6] = charString;
  childArgv[7] = nullptr;
  for (size_t i = 0; i < processorsQuantity - 1; ++i) {
    auto offsetString = std::to_string(i * blockSize);
    childArgv[4] = offsetString.c_str();
    auto idxString = std::to_string(i);
    childArgv[5] = idxString.c_str();
    processes[i] = createProcess(childArgv.data());
  }
  auto offsetString = std::to_string((processorsQuantity - 1) * blockSize);
  blockSizeString = std::to_string(lastBlockSize);
  auto idxString = std::to_string(processorsQuantity-1);
  childArgv[3] = blockSizeString.c_str();
  childArgv[4] = offsetString.c_str();
  childArgv[5] = idxString.c_str();
  processes[processorsQuantity-1] = createProcess(childArgv.data());
  for (auto pid : processes) {
    int result = 0;
    if (waitpid(pid, &result, 0) == -1) {
      printError("Waiting for child failed", errno);
      exit(1);
    }
    if (result != 0) {
      printError("Child process return non-zero exit code", 0);
      exit(1);
    }
  }
  size_t result = 0;
  for (size_t i = 0; i < processorsQuantity; ++i) {
    result += sharedMem[i];
  }
  std::cout << "Result for given file is: " << result << "\n";
  return 0;
}