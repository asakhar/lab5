#ifndef DATA_SOURCE_LAB4_SHARED_MEMORY__HPP
#define DATA_SOURCE_LAB4_SHARED_MEMORY__HPP

#include "errors.hpp"
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

template <typename T> class SharedMem {
public:
  explicit SharedMem(size_t size) : m_size{size} {
    m_fd = open(".dunder_file", O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
    if (m_fd == -1) {
      printError("Failed to open shared file", errno);
      exit(1);
    }
    lseek(m_fd, m_size * sizeof(T) - 1, SEEK_SET);
    write(m_fd, "\0", 1);

    init();
  }
  explicit SharedMem(int fd) : m_fd{fd} {
    struct stat statbuf;
    if (fstat(m_fd, &statbuf) == -1 && statbuf.st_size < sizeof(T)) {
      printError("Getting size of shared file failed", errno);
      _exit(1);
    }
    m_size = statbuf.st_size / sizeof(T);

    init();
  }
  void init() {
    int protection = PROT_READ | PROT_WRITE;

    // The buffer will be shared (meaning other processes can access it), but
    // anonymous (meaning third-party processes cannot obtain an address for
    // it), so only this process and its children will be able to use it:
    int visibility = MAP_SHARED;

    // The remaining parameters to `mmap()` are not important for this use case,
    // but the manpage for `mmap` explains their purpose.
    m_pointer = reinterpret_cast<T *>(
        mmap(NULL, m_size * sizeof(T), protection, visibility, m_fd, 0));
    if (m_pointer == reinterpret_cast<T *>(-1)) {
      printError("Unable to create shared memory", errno);
      _exit(1);
    }
  }
  T &operator[](size_t idx) { return m_pointer[idx]; }
  T *get() const { return m_pointer; }
  int getFd() const { return m_fd; }
  ~SharedMem() {
    if (munmap(reinterpret_cast<void *>(m_pointer), m_size * sizeof(T)) == -1) {
      printError("Unable to destroy shared memory", errno);
      _exit(1);
    }
    if (close(m_fd) == -1) {
      printError("Failed to close shared file", errno);
      _exit(1);
    }
  }

private:
  int m_fd;
  T *m_pointer;
  size_t m_size;
};

#endif // DATA_SOURCE_LAB4_SHARED_MEMORY__HPP