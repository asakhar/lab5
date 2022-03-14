#ifndef TYPE_HPP
#define TYPE_HPP

#include <string>
#include <typeinfo>

std::string demangle(const char* name);

template <class T>
std::string type() {
    return demangle(typeid(T).name());
}

#endif