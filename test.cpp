#include <sched.h>

#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "threads.hpp"

#define race                                     \
  {                                              \
    std::random_device __rd_race{"/dev/random"}; \
    std::bernoulli_distribution __bd_race;       \
    if (__bd_race(__rd_race)) sched_yield();     \
  }

#define ifnot(cond) if (!(cond))

size_t constexpr SIZE = 2000;

int main() {
  SpinLock<std::string> mut{"*"};
  std::stringstream ss;

  auto worka = [mut, &ss]() mutable {
    for (size_t i = 0; i < SIZE; ++i) {
      {
        auto lock = mut.lock();
        {
          ss << "abc" << *lock;
          race;
          ss << "def\n";
        }
      }
      race;
    }
  };
  auto workb = [mut, &ss]() mutable {
    for (size_t i = 0; i < SIZE; ++i) {
      {
        auto lock = mut.lock();
        {
          ss << "123" << *lock;
          race;
          ss << "456\n";
        }
      }
      race;
    }
  };

  std::random_device rd{"/dev/random"};
  std::bernoulli_distribution bd;

  std::vector<Thread> threads;
  for (auto i = 0ul; i < 100; ++i)
    if (bd(rd))
      threads.emplace_back(worka);
    else
      threads.emplace_back(workb);

  for (auto& th : threads) {
    th.join();
  }

  std::string str;
  size_t first = 0, second = 0;
  while (std::getline(ss, str, '\n')) {
    ifnot(str == "abc*def" || str == "123*456") panic("Got invalid line");
    if (str == "abc*def")
      ++first;
    else
      ++second;
  }
  if (first + second != SIZE * threads.size()) panic("Not all lines generated");

  std::cout << "Success\n"
            << "First threads insertions: " << first
            << "\nSecond threads insertions: " << second << std::endl;
  return 0;
}
