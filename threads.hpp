#include <pthread.h>

#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>

class ThreadBase {
 public:
  ThreadBase() = default;
  ~ThreadBase() = default;
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
  Thread(Fn&& fn, Args... args) {
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
  Atomic(T init) {
    static_assert(sizeof(init) <= 8 && std::is_standard_layout_v<T> &&
                      std::is_trivial_v<T>,
                  "Invalid type for atomic");
    std::memcpy(const_cast<void*>(reinterpret_cast<volatile void*>(&value)),
                reinterpret_cast<void const*>(&init), sizeof(init));
  }

  T cas(T cmp, T to) const {
    long long old{};
    long long nwv{};
    std::memcpy(reinterpret_cast<void*>(&old),
                reinterpret_cast<void const*>(&cmp), sizeof(T));
    std::memcpy(reinterpret_cast<void*>(&nwv), reinterpret_cast<void const*>(&to),
                sizeof(T));
    asm volatile(R"(
      mov %0, %%rax
      cmpxchg %1, (%2)
    )"
                 :
                 : "r"(old), "r"(nwv), "r"(&value)
                 : "%rax");
    return get();
  }

  T get() const {
    T res{};
    std::memcpy(
        reinterpret_cast<void*>(&res),
        const_cast<void const*>(reinterpret_cast<volatile void const*>(&value)),
        sizeof(T));
    return res;
  }

  void set_lazy(T val) const {
    while (cas(get(), val) != val) {
    }
  }

  void set(T val) const {
    while (cas(get(), val) != val) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
      pthread_yield();
#pragma GCC diagnostic pop
    }
  }

 private:
  volatile long long mutable value{};
};

template <typename Mut>
class GuardLock {
 public:
  GuardLock(Mut& mut) : mutex{mut} {}
  ~GuardLock() { mutex.unlock(); }
  operator typename Mut::T &() { return *mutex.guardant.get(); }

 private:
  Mut& mutex;
};

template <typename Guardant>
class Mutex {
  using T = Guardant;

 public:
  Mutex(Guardant&& value = {}, bool is_locked = false)
      : guardant(std::make_shared<Guardant>(value)), locked{std::make_shared<Atomic<bool>>(is_locked)} {}

  GuardLock<Mutex> lock() {
    while (locked->cas(false, true) != true) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
      pthread_yield();
#pragma GCC diagnostic pop
    }
    return GuardLock{*this};
  }
  Mutex& operator=(Mutex&& move) {
    guardant = std::move(move.guardant);
    locked = std::move(move.locked);
  }
  Mutex& operator=(Mutex const& copy) {
    guardant = copy.guardant;
    locked = copy.locked;
  }
  Mutex(Mutex&& move) {
    guardant = std::move(move.guardant);
    locked = std::move(move.locked);
  }
  Mutex(Mutex const& copy) {
    guardant = copy.guardant;
    locked = copy.locked;
  }
  friend class GuardLock<Mutex>;

  void unlock() { locked->set(false); }

 private:
  std::shared_ptr<Guardant> guardant;
  std::shared_ptr<Atomic<bool>> locked;
};