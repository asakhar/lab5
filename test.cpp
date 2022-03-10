#include <iostream>

#include "threads.hpp"
#include <sstream>
#include <string>
#include <vector>

using namespace My;

size_t constexpr TSIZE = 200;

int main(int argc, char const* argv[]) {
  My::SpinLock<std::string> mut{"*"};
  std::stringstream ss;


  auto worka = [mut, &ss]() mutable {
      My::SpinLock<std::string> local = mut;
      for (int i = 0; i < TSIZE; ++i) {
      {
        auto lock = local.lock();
        {
          ss << "abc" << *lock;
          SwitchToThread();
          ss << "def\n";
        }
      }
      SwitchToThread();
    }
  };
  /*auto workb = [mut, &ss]() mutable {
    for (int i = 0; i < TSIZE; ++i) {
      {
        auto lock = mut.lock();
        {
          ss << "123" << *lock;
          SwitchToThread();
          ss << "456\n";
        }
      }
      SwitchToThread();
    }
  };*/

  std::vector<Thread> threads;
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
  if(c != TSIZE*threads.size())
    panic("Not all lines generated");

  std::cout << "Success\n";
  return 0;
}
