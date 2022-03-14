#ifndef THREADS_HPP
#define THREADS_HPP

#include <pthread.h>
#include <sched.h>

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <tuple>
#include <type_traits>

#define panic(error)                              \
  {                                               \
    std::cerr << "Error: " << error << std::endl; \
    exit(1);                                      \
  }

class Thread {
 public:
  Thread() { detached = true; }
  template <typename Fn, typename... Args>
  requires std::is_invocable_v<Fn, Args...>
  explicit Thread(Fn&& fn, Args... args) {
    using ArgsContainer = std::pair<Fn, std::tuple<Args...>>;
    auto arg = std::make_unique<ArgsContainer>(
        std::forward<Fn>(fn), std::tuple<Args...>{std::forward<Args>(args)...});
    if (pthread_create(&handle, nullptr, Thread::_start<ArgsContainer>,
                       reinterpret_cast<void*>(arg.get())) == 0) {
      (void)arg.release();
    }
  }
  Thread(Thread const&) = delete;
  Thread& operator=(Thread const&) = delete;
  Thread& operator=(Thread&& move) {
    if (!detached && !joined) {
      void* ret;
      pthread_join(handle, &ret);
    }
    detached = move.detached;
    joined = move.joined;
    handle = move.handle;
    move.handle = 0;
    move.detached = true;
    return *this;
  };
  Thread(Thread&& move) {
    detached = move.detached;
    joined = move.joined;
    handle = move.handle;
    move.handle = 0;
    move.detached = true;
  };
  void join() {
    joined = true;
    void* ret;
    while (pthread_tryjoin_np(handle, &ret) == EBUSY) {
      pthread_kill(handle, SIGCONT);
    }
  }
  bool detach() { return detached = (pthread_detach(handle) == 0); }
  ~Thread() {
    if (!detached && !joined) {
      void* ret;
      pthread_join(handle, &ret);
    }
  }

 private:
  template <typename ArgsContainer>
  static void* _start(void* args) {
    auto arg =
        std::unique_ptr<ArgsContainer>(reinterpret_cast<ArgsContainer*>(args));
    std::apply(arg->first, arg->second);
    pthread_exit(nullptr);
    return nullptr;
  }
  pthread_t handle{};
  bool detached = false;
  bool joined = false;
};

template <typename T>
struct sizeof_t {
  static constexpr size_t value = sizeof(T);
};

template <typename T>
struct sizeof_t<T*> {
  static constexpr size_t value = sizeof(std::intptr_t);
};

template <typename T>
constexpr size_t sizeof_v = sizeof_t<T>::value;

template <typename T>
class Atomic {
 public:
  static size_t constexpr TYPE_SIZE = sizeof_v<T>;
  explicit Atomic(T init = {}) {
    static_assert(
        TYPE_SIZE <= 8 && std::is_standard_layout_v<T> && std::is_trivial_v<T>,
        "Invalid type for atomic");

    asm volatile(R"(
    mfence
    mov %1, (%0)
                    )" ::"r"(value),
                 "r"(init)
                 : "memory");
  }

  Atomic(Atomic&& move) = delete;
  Atomic(Atomic const& copy) = delete;
  Atomic& operator=(Atomic&& move) = delete;
  Atomic& operator=(Atomic const& copy) = delete;

  bool cas(T cmp, T to) const {
    short changed = 0;
    asm volatile(R"(
    mfence
    lock cmpxchg %2, (%3)
    lahf
      )"
                 : "=a"(changed)
                 : "a"(cmp), "r"(to), "r"(value)
                 : "memory");
    return static_cast<bool>(changed & (1 << 14));
  }

  inline T swap(T val) const {
    asm volatile(R"(
    mfence
    lock xchg (%2), %1
      )"
                 : "=a"(val)
                 : "a"(val), "r"(value)
                 : "memory");
    return val;
  }

  inline T get() const {
    T res{};
    asm volatile(R"(
    mfence
    mov (%1), %0
          )"
                 : "=r"(res)
                 : "r"(value)
                 : "memory");
    return res;
  }

 private:
  volatile char mutable value[TYPE_SIZE]{};
  char _padding[16 - TYPE_SIZE];
};

template <typename T>
class Atomic2 {
 public:
  static size_t constexpr TYPE_SIZE = sizeof_v<T>;
  explicit Atomic2(T init = {}) : value{init} {
    static_assert(
        TYPE_SIZE <= 8 && std::is_standard_layout_v<T> && std::is_trivial_v<T>,
        "Invalid type for atomic");
  }

  Atomic2(Atomic2&& move) = delete;
  Atomic2(Atomic2 const& copy) = delete;
  Atomic2& operator=(Atomic2&& move) = delete;
  Atomic2& operator=(Atomic2 const& copy) = delete;

  bool cas(T cmp, T to) const {
    short changed = 0;
    asm volatile(R"(
    mfence
    lock cmpxchg %2, (%3)
    lahf
      )"
                 : "=a"(changed)
                 : "a"(cmp), "r"(to), "r"(&value)
                 : "memory");
    return static_cast<bool>(changed & (1 << 14));
  }

  inline T swap(T val) const {
    asm volatile(R"(
    mfence
    lock xchg (%2), %1
      )"
                 : "=a"(val)
                 : "a"(val), "r"(&value)
                 : "memory");
    return val;
  }

  inline T fetch_add(T add) const {
    T prev;
    while (prev = get(), !cas(prev, prev + add))
      ;
    return prev;
  }

  inline T fetch_sub(T add) const {
    T prev;
    while (prev = get(), !cas(prev, prev - add))
      ;
    return prev;
  }

  inline T fetch_mul(T add) const {
    T prev;
    while (prev = get(), !cas(prev, prev * add))
      ;
    return prev;
  }

  inline T fetch_div(T add) const {
    T prev;
    while (prev = get(), !cas(prev, prev / add))
      ;
    return prev;
  }

  inline T get() const { return value; }

 private:
  volatile T mutable value;
  char _padding[16 - TYPE_SIZE];
};

template <typename Mut>
class GuardLock {
 public:
  GuardLock(Mut& mut) : mutex{mut} {}
  ~GuardLock() { mutex.unlock(); }
  operator typename Mut::Type &() { return mutex.deref_unchecked(); }
  typename Mut::Type& operator*() { return mutex.deref_unchecked(); }
  typename Mut::Type* operator->() { return &mutex.deref_unchecked(); }

 private:
  Mut& mutex;
};

template <typename Guardant>
class SpinLock {
  using Type = Guardant;

 public:
  SpinLock(Type&& value = {}, bool is_locked = false)
      : control{
            std::make_shared<Control>(std::forward<Type>(value), is_locked)} {}

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
  Type& deref_unchecked() { return control->guardant; }
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
    if (control->locked.swap(false) == false)
      panic("Trying to unlock already unlocked spinlock.");
  }
  struct Control {
    Control(Type&& g, bool l) : guardant{std::forward<Type>(g)}, locked{l} {}
    Type guardant;
    Atomic2<bool> locked;
  };
  std::shared_ptr<Control> control;
};

#if 0
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
    else if (reinterpret_cast<unsigned long long>(prev) & 1ull)
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
    if (node->next.get() == nullptr) {
      tail.swap(nullptr);
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
  using Type = Guardant;

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
  Guardant& deref_unchecked() { return control->guardant; }
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
#endif

#include <queue>

#include "type.hpp"

#define todo(msg) panic("TODO: " msg)

#define dbg(code)

template <typename Guardant>
class Mutex {
  using Type = Guardant;

 public:
  explicit Mutex(Type&& value = Type{}, bool is_locked = false)
      : control{
            std::make_shared<Control>(std::forward<Type>(value), is_locked)} {}

  GuardLock<Mutex> lock() {
    while (control->locked.swap(true) == true) {
      { control->wait_list.lock()->push(pthread_self()); }
      pthread_cancel(pthread_self());
    }
    return GuardLock{*this};
  }
  std::optional<GuardLock<Mutex>> try_lock() {
    if (control->locked.swap(true) == false) return {*this};
    return {};
  }
  Type& deref_unchecked() { return control->guardant; }
  Mutex& operator=(Mutex&& move) { control = std::move(move.control); }
  Mutex& operator=(Mutex const& copy) { control = copy.control; }
  Mutex(Mutex&& move) { control = std::move(move.control); }
  Mutex(Mutex const& copy) { control = copy.control; }
  ~Mutex() {
    // auto queue = control->wait_list.lock();
    // if (!queue->empty()) {
    //   auto waited = queue->front();
    //   queue->pop();
    //   if (waited != pthread_self()) pthread_kill(waited, SIGCONT);
    // }
  }
  friend class GuardLock<Mutex>;

 private:
  void unlock() {
    if (control->locked.swap(false) == false)
      panic("Unlocked not locked mutex");
    auto queue = control->wait_list.lock();
    if (!queue->empty()) {
      auto waited = queue->front();
      queue->pop();
      pthread_kill(waited, SIGCONT);
    }
  }
  struct Control {
   public:
    Control(Type&& g, bool l)
        : guardant{std::forward<Type>(g)}, locked{l}, wait_list{} {}
    Type guardant;
    Atomic2<bool> locked;
    SpinLock<std::queue<pthread_t>> wait_list;
  };
  std::shared_ptr<Control> control;
};

// template <typename Guardant>
// class Mutex2 {
//   using Type = Guardant;

//  public:
//   explicit Mutex2(Guardant&& value = Guardant{}, bool is_locked = false)
//       : control{std::make_shared<Control>(std::forward<Guardant>(value),
//                                           is_locked ? this : nullptr)},
//         thread(pthread_self()) {
//     id = control->count.fetch_add(1);
//   }

//   GuardLock<Mutex2> lock() {
//     Mutex2<Type>* prev = nullptr;
//     dbg({
//       std::stringstream ss;
//       ss << "trying to lock " << id << "\n";
//       std::cout << ss.str();
//     });
//     while ((prev = control->locked.swap((Mutex2<Type>*)-1)) != nullptr) {
//       if (prev == (Mutex2<Type>*)-1) {
//         sched_yield();
//         continue;
//       }
//       thread = pthread_self();
//       auto prev_pend = prev;
//       while (!prev_pend->pending.cas(nullptr, this) &&
//              prev_pend->pending.get() != (void*)this) {
//         prev_pend = prev_pend->pending.get();
//       }
//       if (control->locked.swap(prev) != (Mutex2<Type>*)-1)
//         panic("Changed locked value");
//       pause();
//     }
//     dbg({
//       std::stringstream ss;
//       ss << "locked   " << id << "\n";
//       std::cout << ss.str();
//     });
//     GuardLock lock{*this};
//     if (control->locked.swap(this) != (Mutex2<Type>*)-1)
//       panic("Changed locked value");
//     return lock;
//   }
//   std::optional<GuardLock<Mutex2>> try_lock() {
//     todo("change implementation");
//     if (control->locked.swap(true) == false) return {*this};
//     return {};
//   }
//   Guardant& deref_unchecked() { return control->guardant; }
//   Mutex2& operator=(Mutex2&& move) {
//     control = std::move(move.control);
//     id = move.id;
//   }
//   Mutex2& operator=(Mutex2 const& copy) {
//     dbg(std::cout << "Mutex copy assign\n";);
//     control = copy.control;
//     id = control->count.fetch_add(1);
//   }
//   Mutex2(Mutex2&& move) : control{std::move(move.control)} { id = move.id; }
//   Mutex2(Mutex2 const& copy) : control{copy.control} {
//     id = control->count.fetch_add(1);
//     std::stringstream ss;
//     // ss << "Mutex copy " << type<decltype(this)>() << "@" << id << "\n";
//     std::cout << ss.str();
//   }
//   size_t getid() const { return id; }
//   friend class GuardLock<Mutex2>;

//  private:
//   void unlock() {
//     void* prev = nullptr;
//     while ((prev = control->locked.swap((Mutex2<Type>*)-1)) != this) {
//       if (prev == (void*)-1) {
//         continue;
//       }
//       panic("Somebody has taken my lock");
//     }
//     auto pend = pending.swap(nullptr);
//     dbg({
//       std::stringstream ss;
//       ss << "unlocked " << id << "\n";
//       std::cout << ss.str();
//     });
//     if (control->locked.swap(nullptr) != (void*)-1)
//       panic("Changed locked value");
//     if (pend != nullptr) {
//       auto next_pend = (Mutex2<Type>*)pend;
//       dbg({
//         std::stringstream ss;
//         ss << "Woken up " << next_pend->id << "\n";
//         std::cout << ss.str();
//       });
//       pthread_kill(next_pend->thread, SIGCONT);
//     }
//   }
//   struct Control {
//    public:
//     Control(Guardant&& g, Mutex2<Type>* l)
//         : guardant{std::forward<Guardant>(g)}, locked{l} {}
//     Guardant guardant;
//     Atomic2<Mutex2<Type>*> locked;
//     Atomic2<size_t> count{0};
//   };
//   std::shared_ptr<Control> control;
//   Atomic2<Mutex2<Type>*> pending{nullptr};
//   pthread_t thread;
//   size_t id;
// };
#endif  // THREADS_HPP