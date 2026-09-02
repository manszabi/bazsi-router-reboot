#include "strutil.h"

#include <ctype.h>
#include <string.h>

void trimInPlace(char* s) {
  size_t len = strlen(s);
  while (len > 0 && isspace((unsigned char)s[len - 1])) {
    s[--len] = '\0';
  }
  size_t start = 0;
  while (s[start] != '\0' && isspace((unsigned char)s[start])) {
    start++;
  }
  if (start > 0) {
    memmove(s, s + start, len - start + 1);
  }
}
