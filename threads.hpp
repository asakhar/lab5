#ifndef THREADS_HPP
#define THREADS_HPP
#include <iostream>
#include <sstream>
#include <tuple>

#ifdef __linux__
#include <optional>
#include <pthread.h>
#include <sched.h>

#elif _WIN32
#include <windows.h>
namespace std {
struct nullopt_t {};
constexpr nullopt_t nullopt;
template <typename T> class optional {
public:
  T &operator*() { return value; }
  optional() : contains_value(false) {}
  optional(T val) : value{val}, contains_value{true} {}
  bool operator==(nullopt_t) { return !contains_value; }
  bool operator!=(nullopt_t) { return contains_value; }

private:
  T value;
  bool contains_value;
};
namespace detail {
template <class F, class Tuple, std::size_t... I>
constexpr decltype(auto) apply_impl(F &&f, Tuple &&t,
                                    std::index_sequence<I...>) {
  // This implementation is valid since C++20 (via P1065R2)
  // In C++17, a constexpr counterpart of std::invoke is actually needed here
  return std::invoke(std::forward<F>(f),
                     std::get<I>(std::forward<Tuple>(t))...);
}
} // namespace detail

template <class F, class Tuple>
constexpr decltype(auto) apply(F &&f, Tuple &&t) {
  return detail::apply_impl(
      std::forward<F>(f), std::forward<Tuple>(t),
      std::make_index_sequence<
          std::tuple_size_v<std::remove_reference_t<Tuple>>>{});
}
}; // namespace std
#endif

#include <functional>
#include <memory>

#include <type_traits>

#define panic(error)                                                           \
  {                                                                            \
    std::fprintf(stderr, "Error: %s", error);                                  \
    exit(1);                                                                   \
  }
namespace My {
class Thread {
public:
  Thread() { detached = true; }
  template <typename Fn, typename... Args>
#ifdef __linux__
  requires std::is_invocable_v<Fn, Args...>
#endif
  explicit Thread(Fn &&fn, Args... args) {
    using ArgsContainer = std::pair<Fn, std::tuple<Args...>>;
    auto arg = std::make_unique<ArgsContainer>(
        std::forward<Fn>(fn), std::tuple<Args...>{std::forward<Args>(args)...});
    DWORD threadId;
    handle = CreateThread(NULL, 0, Thread::_start<ArgsContainer>,
                          reinterpret_cast<void *>(arg.get()), 0, &threadId);
    if (handle != NULL) {
      (void)arg.release();
    }
  }
  Thread(Thread const &) = delete;
  Thread &operator=(Thread const &) = delete;
  Thread &operator=(Thread &&move) {
    if (!detached && !joined) {
      WaitForSingleObject(handle, INFINITE);
    }
    detached = move.detached;
    joined = move.joined;
    handle = move.handle;
    move.handle = NULL;
    move.detached = true;
    return *this;
  };
  Thread(Thread &&move) {
    detached = move.detached;
    joined = move.joined;
    handle = move.handle;
    move.handle = NULL;
    move.detached = true;
  };
  void join() {
    joined = true;
    ResumeThread(handle);
    WaitForSingleObject(handle, INFINITE);
  }
  bool detach() { return detached = true; }
  ~Thread() {
    if (!detached && !joined) {
      join();
    }
  }

private:
  template <typename ArgsContainer> static unsigned long _start(void *args) {
    auto arg =
        std::unique_ptr<ArgsContainer>(reinterpret_cast<ArgsContainer *>(args));
    std::apply(arg->first, arg->second);
    return 0;
  }
  HANDLE handle{};
  bool detached = false;
  bool joined = false;
};

template <typename T> struct sizeof_t {
  static constexpr size_t value = sizeof(T);
};

template <typename T> struct sizeof_t<T *> {
  static constexpr size_t value = sizeof(std::intptr_t);
};

template <typename T> constexpr size_t sizeof_v = sizeof_t<T>::value;

template <typename T> class Atomic {
public:
  static size_t constexpr TYPE_SIZE = sizeof_v<T>;
  explicit Atomic(T init = {}) {
    static_assert(TYPE_SIZE <= 8 && std::is_standard_layout_v<T> &&
                      std::is_trivial_v<T>,
                  "Invalid type for atomic");

    asm volatile(R"(
                        mov %1, (%0)
                    )" ::"r"(value),
                 "r"(init));
  }

  Atomic(Atomic &&move) = delete;
  Atomic(Atomic const &copy) = delete;
  Atomic &operator=(Atomic &&move) = delete;
  Atomic &operator=(Atomic const &copy) = delete;

  bool cas(T cmp, T to) const {
    short changed = 0;
    __asm__ volatile(R"(
        lock cmpxchg %2, (%3)
        lahf
      )"
                     : "=a"(changed)
                     : "a"(cmp), "r"(to), "r"(value)
                     :);
    return static_cast<bool>(changed & (1 << 14));
  }

  inline T swap(T val) const {
    __asm__ volatile(R"(
        lock xchg (%2), %1
      )"
                     : "=a"(val)
                     : "a"(val), "r"(value));
    return val;
  }

  inline T get() const {
    T res{};
    __asm__ volatile(R"(
              mov (%1), %0
          )"
                     : "=r"(res)
                     : "r"(value)
                     :);
    return res;
  }

private:
  volatile char mutable value[TYPE_SIZE]{};
  char _padding[16 - TYPE_SIZE];
};

template <typename Mut> class GuardLock {
public:
  GuardLock(Mut &mut) : mutex{mut} {}
  ~GuardLock() {
    // std::cout << "~GuardLock()" << std::endl;
    mutex.unlock();
  }
  operator typename Mut::Type &() { return mutex.deref_unchecked(); }
  typename Mut::Type &operator*() { return mutex.deref_unchecked(); }

private:
  Mut &mutex;
};

template <typename Guardant> class SpinLock {
  using Type = Guardant;

public:
  SpinLock(Type &&value = Type{}, bool is_locked = false)
      : control{
            std::make_shared<Control>(std::forward<Type>(value), is_locked)} {}

  GuardLock<SpinLock> lock() {
    while (control->locked.swap(true) != false) {
      SwitchToThread();
    }
    return GuardLock{*this};
  }
  std::optional<GuardLock<SpinLock>> try_lock() {
    if (control->locked.swap(true) == false)
      return {*this};
    return {};
  }
  Guardant &deref_unchecked() { return control->guardant; }
  SpinLock &operator=(SpinLock &&move) {
    control = std::move(move.control);
    return *this;
  }
  SpinLock &operator=(SpinLock const &copy) {
    control = copy.control;
    return *this;
  }
  SpinLock(SpinLock &&move) { control = std::move(move.control); }
  SpinLock(SpinLock const &copy) { control = copy.control; }
  friend class GuardLock<SpinLock>;

private:
  void unlock() {
    if (control->locked.swap(false) == false)
      panic("Trying to unlock already unlocked spinlock.");
  }
  struct Control {
    Control(Type &&g, bool l) : guardant{std::forward<Type>(g)}, locked{l} {}
    Guardant guardant;
    Atomic<bool> locked;
  };
  std::shared_ptr<Control> control;
};

template <typename Guardant> class CritSec {
  using Type = Guardant;

public:
  CritSec(Type &&value = Type{}, bool is_locked = false)
      : control{std::make_shared<Control>(std::forward<Type>(value))} {
    lock();
  }

  GuardLock<CritSec> lock() {
    EnterCriticalSection(&control->locked);
    return GuardLock{*this};
  }
  Guardant &deref_unchecked() { return control->guardant; }
  CritSec &operator=(CritSec &&move) {
    control = std::move(move.control);
    return *this;
  }
  CritSec &operator=(CritSec const &copy) {
    control = copy.control;
    return *this;
  }
  CritSec(CritSec &&move) { control = std::move(move.control); }
  CritSec(CritSec const &copy) { control = copy.control; }
  friend class GuardLock<CritSec>;

private:
  void unlock() { LeaveCriticalSection(&control->locked); }
  struct Control {
    Control(Guardant &&g) : guardant{std::forward<Type>(g)} {
      InitializeCriticalSection(&locked);
    }
    ~Control() { DeleteCriticalSection(&locked); }
    Guardant guardant;
    CRITICAL_SECTION locked{};
  };
  std::shared_ptr<Control> control;
};

template <typename T> class CLIFO {
public:
  CLIFO() = default;
  void push(T value) {
    auto node = new Node{Atomic<Node *>{nullptr}, std::forward<T>(value)};
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
    if (node == nullptr)
      return {};
    if (reinterpret_cast<unsigned long long>(node) & 1ull) {
      goto begin_loop;
    }
    node_prot = reinterpret_cast<Node *>(
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
  CLIFO &operator=(CLIFO const &) = delete;
  CLIFO &operator=(CLIFO &&) = delete;
  CLIFO(CLIFO const &) = delete;
  CLIFO(CLIFO &&) = delete;
  ~CLIFO() {
    while (pop() != std::nullopt)
      ;
  }

private:
  struct Node {
    Atomic<Node *> prev;
    T value;
  };
  Atomic<Node *> tail{nullptr};
};

template <typename T> class CPEPQ {
public:
  CPEPQ() = default;
  void push(T value) {
    auto node = new Node{Atomic<Node *>{nullptr}, std::forward<T>(value)};
    auto prev = tail.swap(node);
    if (prev == nullptr)
      head.cas(nullptr, node);
    else if (reinterpret_cast<unsigned long long>(prev) & 1ull)
      prev->next.swap(node);
  }
  std::optional<T> pop() {
    Node *node, *node_prot;
  begin_loop : {
    node = head.get();
    if (node == nullptr)
      return {};
    if (reinterpret_cast<unsigned long long>(node) & 1ull) {
      goto begin_loop;
    }
    node_prot = reinterpret_cast<Node *>(
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
  CPEPQ &operator=(CPEPQ const &) = delete;
  CPEPQ &operator=(CPEPQ &&) = delete;
  CPEPQ(CPEPQ const &) = delete;
  CPEPQ(CPEPQ &&) = delete;
  ~CPEPQ() {
    while (pop() != std::nullopt)
      ;
  }

private:
  struct Node {
    Atomic<Node *> next;
    T value;
  };
  Atomic<Node *> tail{nullptr};
  Atomic<Node *> head{nullptr};
};

template <typename Guardant> class Mutex {
  using Type = Guardant;

public:
  explicit Mutex(Guardant &&value = {}, bool is_locked = false)
      : control{std::make_shared<Control>(value, is_locked)} {}

  GuardLock<Mutex> lock() {
    while (control->locked.swap(true) == true) {
      auto current = GetCurrentThread();
      control->wait_list.push(current);
      std::stringstream ss;
      ss << "suspended " << current << std::endl;
      std::cout << ss.str();
      SuspendThread(current);
    }
    return GuardLock{*this};
  }
  std::optional<GuardLock<Mutex>> try_lock() {
    if (control->locked.swap(true) == false)
      return {*this};
    return {};
  }
  Guardant &deref_unchecked() { return control->guardant; }
  Mutex &operator=(Mutex &&move) { control = std::move(move.control); }
  Mutex &operator=(Mutex const &copy) { control = copy.control; }
  Mutex(Mutex &&move) { control = std::move(move.control); }
  Mutex(Mutex const &copy) { control = copy.control; }
  ~Mutex() {
    std::cout << "~Mutex()" << std::endl;
    unlock();
  }
  friend class GuardLock<Mutex>;

private:
  void unlock() {
    control->locked.swap(false);
    std::optional<HANDLE> waited;
    do {
      waited = control->wait_list.pop();
      if (waited == std::nullopt) {
        break;
      }
      std::stringstream ss;
      ss << "resumed " << *waited << std::endl;
      std::cout << ss.str();
      ResumeThread(*waited);
      /*if (control.use_count() != 1)
          break;*/
    } while (waited != std::nullopt);
  }
  struct Control {
  public:
    Control(Guardant g, bool l) : guardant{g}, locked{l}, wait_list{} {}
    Guardant guardant;
    Atomic<bool> locked;
    CPEPQ<HANDLE> wait_list;
  };
  std::shared_ptr<Control> control;
};
} // namespace My
#endif // THREADS_HPP