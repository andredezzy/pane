// OpenBSD's bcrypt (lib/libc/crypt/bcrypt.c, vendored unmodified beside this
// header; third_party/openbsd-bcrypt/README.md): the two calls PaneKit
// uses.

#ifndef CBCRYPT_H_
#define CBCRYPT_H_

#include <stddef.h>

// Hashes `pass` with 2^`log_rounds` rounds and a random salt into `hash`
// ("$2b$…", 61 bytes with its terminator). 0 on success, -1 on failure.
int bcrypt_newhash(const char* pass, int log_rounds, char* hash, size_t hashlen);

// 0 when `pass` matches `goodhash` ("$2a$" or "$2b$"), -1 otherwise.
int bcrypt_checkpass(const char* pass, const char* goodhash);

#endif  // CBCRYPT_H_
