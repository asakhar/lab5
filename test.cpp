#include <sched.h>

#include <iostream>

#include "threads.hpp"
#include <sstream>
#include <string>
#include <vector>

size_t constexpr SIZE = 2000;

int main(int argc, char const* argv[]) {
  SpinLock<std::string> mut{"*"};
  std::stringstream ss;


  auto worka = [mut, &ss]() mutable {
    for (int i = 0; i < SIZE; ++i) {
      {
        auto lock = mut.lock();
        {
          ss << "abc" << *lock;
          sched_yield();
          ss << "def\n";
        }
      }
      sched_yield();
    }
  };
  auto workb = [mut, &ss]() mutable {
    for (int i = 0; i < SIZE; ++i) {
      {
        auto lock = mut.lock();
        {
          ss << "123" << *lock;
          sched_yield();
          ss << "456\n";
        }
      }
      sched_yield();
    }
  };

  std::vector<Thread<decltype(worka)&>> threads;
  for(auto i = 0ul; i < 100; ++i)
    threads.emplace_back(worka);

  for(auto& th : threads) {
    th.join();
  }
  // Thread a{worka};
  // Thread<decltype(workb)&> b(workb);

  std::string str;
  size_t c = 0;
  while(std::getline(ss, str, '\n')) {
    if(str == "abc*def" || str == "123*456")
      c++;
    else 
      panic("Got invalid line");
  }
  if(c != SIZE*threads.size())
    panic("Not all lines generated");

  std::cout << "Success\n";
  return 0;
}
