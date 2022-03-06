#ifndef THREADS_HPP
#define THREADS_HPP

#include <pthread.h>
#include <sched.h>

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>

#define panic(error) {std::fprintf(stderr, "Error: %s", error); exit(1);}

class ThreadBase {
 public:
  ThreadBase() = default;
  virtual ~ThreadBase() = default;
  ThreadBase(ThreadBase const&) = delete;
  ThreadBase& operator=(ThreadBase const&) = delete;
  ThreadBase& operator=(ThreadBase&&) = delete;
  ThreadBase(ThreadBase&&) = delete;

 protected:
  pthread_t handle{};
  bool detached = false;
  bool joined = false;
};

template <typename T, typename U>
struct pair {
  T first;
  U second;
};

template <typename Fn, typename... Args>
class Thread : ThreadBase {
  using ArgsContainer = pair<Fn, std::tuple<Args...>>;
  using Return_t = std::invoke_result_t<Fn, Args...>;

 public:
  Thread() { detached = true; }
  explicit Thread(Fn&& fn, Args... args) {
    static constexpr auto deleter = [](ArgsContainer* ptr) {
      ptr->~ArgsContainer();
      std::free(ptr);
    };
    auto ptr = std::unique_ptr<ArgsContainer, decltype(deleter)>(
        reinterpret_cast<ArgsContainer*>(malloc(sizeof(ArgsContainer))),
        deleter);
    auto arg = new (ptr.get())
        ArgsContainer{std::forward<Fn>(fn), {std::forward<Args>(args)...}};
    if (pthread_create(&handle, nullptr, Thread::_start,
                       reinterpret_cast<void*>(arg)) == 0) {
      (void)ptr.release();
    }
  }
  Thread(Thread const&) = delete;
  Thread& operator=(Thread const&) = delete;
  Thread& operator=(Thread&& move) {
    if (!detached && !joined) {
      void* ret;
      pthread_join(handle, &ret);
      if constexpr (!std::is_same_v<Return_t, void>) {
        std::free(ret);
      }
    }
    detached = move.detached;
    joined = move.joined;
    handle = move.handle;
    move.handle = 0;
    move.detached = true;
  };
  Thread(Thread&& move) {
    detached = move.detached;
    joined = move.joined;
    handle = move.handle;
    move.handle = 0;
    move.detached = true;
  };
  auto join() {
    joined = true;
    void* ret;
    pthread_join(handle, &ret);
    if constexpr (!std::is_same_v<Return_t, void>) {
      auto res = *reinterpret_cast<Return_t*>(ret);
      reinterpret_cast<ArgsContainer*>(ret)->~ArgsContainer();
      std::free(ret);
      return res;
    }
  }
  bool detach() { return detached = (pthread_detach(handle) == 0); }
  ~Thread() {
    if (!detached && !joined) {
      void* ret;
      pthread_join(handle, &ret);
      if constexpr (!std::is_same_v<Return_t, void>) {
        std::free(ret);
      }
    }
  }

 private:
  static void* _start(void* args) {
    auto arg = reinterpret_cast<ArgsContainer*>(args);
    if constexpr (!std::is_same_v<Return_t, void>) {
      static constexpr auto deleter = [](Return_t* ptr) {
        ptr->~Return_t();
        std::free(ptr);
      };
      auto ptr = std::unique_ptr<Return_t, decltype(deleter)>(
          reinterpret_cast<Return_t*>(malloc(sizeof(Return_t))), deleter);
      auto ret = new (ptr.get()) Return_t{std::apply(arg->first, arg->second)};
      std::free(arg);
      return ptr.release();
    }
    std::apply(arg->first, arg->second);
    return nullptr;
  }
};

template <typename Fn, typename... Args>
Thread(Fn const&, Args... args) -> Thread<Fn const&, Args...>;
template <typename Fn, typename... Args>
Thread(Fn&, Args... args) -> Thread<Fn&, Args...>;

template <typename T>
class Atomic {
 public:
  explicit Atomic(T init = {}) {
    static_assert(sizeof(init) <= 8 && std::is_standard_layout_v<T> &&
                      std::is_trivial_v<T>,
                  "Invalid type for atomic");

    asm volatile(R"(
                        mov %1, (%0)
                    )" ::"r"(value),
                 "r"(init));
  }

  Atomic(Atomic&& move) = delete;
  Atomic(Atomic const& copy) = delete;
  Atomic& operator=(Atomic&& move) = delete;
  Atomic& operator=(Atomic const& copy) = delete;

  bool cas(T cmp, T to) const {
    short changed = 0;
    asm volatile(R"(
        lock cmpxchg %2, (%3)
        lahf
      )"
                 : "=a"(changed)
                 : "a"(cmp), "r"(to), "r"(value)
                 :);
    return static_cast<bool>(changed & (1 << 14));
  }

  inline T swap(T val) const {
    asm volatile(R"(
        lock xchg (%2), %1
      )"
                 : "=a"(val)
                 : "a"(val), "r"(value));
    return val;
  }

  inline T get() const {
    T res{};
    asm volatile(R"(
              mov (%1), %0
          )"
                 : "=r"(res)
                 : "r"(value)
                 :);
    return res;
  }

 private:
  volatile char mutable value[sizeof(T)]{};
  char _padding[16 - sizeof(T)];
};

template <typename Mut>
class GuardLock {
 public:
  GuardLock(Mut& mut) : mutex{mut} {}
  ~GuardLock() { mutex.unlock(); }
  operator typename Mut::T &() { return mutex.control->guardant; }
  typename Mut::T& operator*() { return mutex.control->guardant; }

 private:
  Mut& mutex;
};

template <typename Guardant>
class SpinLock {
  using T = Guardant;

 public:
  SpinLock(Guardant&& value = {}, bool is_locked = false)
      : control{std::make_shared<Control>(value, is_locked)} {}

  GuardLock<SpinLock> lock() {
    while (control->locked.swap(true) != false) {
      sched_yield();
    }
    return GuardLock{*this};
  }
  std::optional<GuardLock<SpinLock>> try_lock() {
    if (control->locked.swap(true) == false) return {*this};
    return {};
  }
  SpinLock& operator=(SpinLock&& move) {
    control = std::move(move.control);
    return *this;
  }
  SpinLock& operator=(SpinLock const& copy) {
    control = copy.control;
    return *this;
  }
  SpinLock(SpinLock&& move) { control = std::move(move.control); }
  SpinLock(SpinLock const& copy) { control = copy.control; }
  friend class GuardLock<SpinLock>;

 private:
  void unlock() {
    if (control->locked.swap(false) == false) exit(1);
  }
  struct Control {
    Control(Guardant g, bool l) : guardant{g}, locked{l} {}
    Guardant guardant;
    Atomic<bool> locked;
  };
  std::shared_ptr<Control> control;
};

template <typename T>
class CLIFO {
 public:
  CLIFO() = default;
  void push(T value) {
    auto node = new Node{Atomic<Node*>{nullptr}, std::forward<T>(value)};
    do {
    begin_loop:
      node->prev.swap(tail.get());
      if (reinterpret_cast<unsigned long long>(node->prev.get()) & 1ull) {
        goto begin_loop;
      }
    } while (!tail.cas(node->prev.get(), node));
  }
  std::optional<T> pop() {
    Node *node, *node_prot;
  begin_loop : {
    node = tail.get();
    if (node == nullptr) return {};
    if (reinterpret_cast<unsigned long long>(node) & 1ull) {
      goto begin_loop;
    }
    node_prot = reinterpret_cast<Node*>(
        reinterpret_cast<unsigned long long>(node) | 1ull);
    if (!tail.cas(node, node_prot)) {
      goto begin_loop;
    }
  }
    if (!tail.cas(node_prot, node->prev.get())) {
      panic("Error protected node changed!");
    }
    T res = std::move(node->value);
    delete node;
    return res;
  }
  CLIFO& operator=(CLIFO const&) = delete;
  CLIFO& operator=(CLIFO&&) = delete;
  CLIFO(CLIFO const&) = delete;
  CLIFO(CLIFO&&) = delete;
  ~CLIFO() {
    while (pop() != std::nullopt)
      ;
  }

 private:
  struct Node {
    Atomic<Node*> prev;
    T value;
  };
  Atomic<Node*> tail{nullptr};
};

template <typename T>
class CPEPQ {
 public:
  CPEPQ() = default;
  void push(T value) {
    auto node = new Node{Atomic<Node*>{nullptr}, std::forward<T>(value)};
    auto prev = tail.swap(node);
    if (prev == nullptr)
      head.cas(nullptr, node);
    else if (reinterpret_cast<unsigned long long>(prev) & 1ull != 1)
      prev->next.swap(node);
  }
  std::optional<T> pop() {
    Node *node, *node_prot;
  begin_loop : {
    node = head.get();
    if (node == nullptr) return {};
    if (reinterpret_cast<unsigned long long>(node) & 1ull) {
      goto begin_loop;
    }
    node_prot = reinterpret_cast<Node*>(
        reinterpret_cast<unsigned long long>(node) | 1ull);
    if (!head.cas(node, node_prot)) {
      goto begin_loop;
    }
  }
    if (!head.cas(node_prot, node->next.get())) {
      panic("Error: protected node changed!");
    }

    T res = std::move(node->value);
    delete node;
    return res;
  }
  CPEPQ& operator=(CPEPQ const&) = delete;
  CPEPQ& operator=(CPEPQ&&) = delete;
  CPEPQ(CPEPQ const&) = delete;
  CPEPQ(CPEPQ&&) = delete;
  ~CPEPQ() {
    while (pop() != std::nullopt)
      ;
  }

 private:
  struct Node {
    Atomic<Node*> next;
    T value;
  };
  Atomic<Node*> tail{nullptr};
  Atomic<Node*> head{nullptr};
};

template <typename Guardant>
class Mutex {
  using T = Guardant;

 public:
  explicit Mutex(Guardant&& value = {}, bool is_locked = false)
      : control{std::make_shared<Control>(value, is_locked)} {}

  GuardLock<Mutex> lock() {
    while (control->locked.swap(true) == true) {
      control->wait_list.push(pthread_self());
      pthread_cancel(pthread_self());
    }
    return GuardLock{*this};
  }
  std::optional<GuardLock<Mutex>> try_lock() {
    if (control->locked.swap(true) == false) return {*this};
    return {};
  }
  Mutex& operator=(Mutex&& move) { control = std::move(move.control); }
  Mutex& operator=(Mutex const& copy) { control = copy.control; }
  Mutex(Mutex&& move) { control = std::move(move.control); }
  Mutex(Mutex const& copy) { control = copy.control; }
  friend class GuardLock<Mutex>;

 private:
  void unlock() {
    control->locked.swap(false);
    auto waited = control->wait_list.pop();
    if (waited != std::nullopt) {
      pthread_kill((*waited), SIGCONT);
    }
  }
  struct Control {
   public:
    Control(Guardant g, bool l) : guardant{g}, locked{l}, wait_list{} {}
    Guardant guardant;
    Atomic<bool> locked;
    CPEPQ<pthread_t> wait_list;
  };
  std::shared_ptr<Control> control;
};
#endif  // THREADS_HPP