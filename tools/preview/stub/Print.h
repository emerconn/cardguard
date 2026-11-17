#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t *b, size_t n) { size_t i = 0; while (i < n) write(b[i++]); return n; }
  size_t print(const char *s) { return write((const uint8_t *)s, strlen(s)); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(int v) { char b[16]; snprintf(b, sizeof b, "%d", v); return print(b); }
  size_t println(const char *s = "") { return print(s) + print("\n"); }
  size_t print(const struct String &s);
  size_t print(const class __FlashStringHelper *s) { return print((const char *)s); }
};
class __FlashStringHelper;
struct String {
  const char *s = "";
  const char *c_str() const { return s; }
  unsigned int length() const { return strlen(s); }
};
inline size_t Print::print(const String &s) { return print(s.c_str()); }
