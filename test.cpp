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

template<typename T>
T copy(T const& val) {
  return val;
}

size_t constexpr SIZE = 20;

int main() {
  Mutex<std::stringstream> ssmut{};

  auto worka = [ssmut]() mutable {
    for (size_t i = 0; i < SIZE; ++i) {
      {
        dbg(std::cout << "entered " << ssmut.getid() << "\n";);
        auto ss = ssmut.lock();
        {
          dbg(std::cout << "working " << ssmut.getid() << "\n";);
          *ss << "abc";
          race;
          *ss << "def\n";
          ss->flush();
        }
      }
      race;
    }
  };
  auto workb = [ssmut]() mutable {
    for (size_t i = 0; i < SIZE; ++i) {
      {
        dbg(std::cout << "entered " << ssmut.getid() << "\n";);
        auto ss = ssmut.lock();
        {
          dbg(std::cout << "working " << ssmut.getid() << "\n";);
          *ss << "123";
          race;
          *ss << "456\n";
          ss->flush();
        }
      }
      race;
    }
  };

  std::random_device rd{"/dev/random"};
  std::bernoulli_distribution bd;

  std::vector<Thread> threads;
  for (auto i = 0ul; i < 4; ++i)
    if (bd(rd))
      threads.emplace_back(copy(worka));
    else
      threads.emplace_back(copy(workb));

  for (auto& th : threads) {
    th.join();
  }

  std::string str;
  size_t first = 0, second = 0;
  std::cout << ssmut.deref_unchecked().str();
  while (std::getline(ssmut.deref_unchecked(), str, '\n')) {
    ifnot(str == "abcdef" || str == "123456") panic("Got invalid line");
    if (str == "abcdef")
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
