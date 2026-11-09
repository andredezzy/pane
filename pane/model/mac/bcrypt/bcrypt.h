// Copyright 2026 Pane. Licensed under GPL-3.0-only.

// OpenBSD's bcrypt (lib/libc/crypt/bcrypt.c, vendored unmodified beside this
// header; third_party/openbsd-bcrypt/README.md): the two calls Pane uses.
// Pane checks a PIN against the hash the Electron app and 0.13.1 and before
// wrote (VerifyPinHash); only its test makes one.

#ifndef PANE_MODEL_MAC_BCRYPT_BCRYPT_H_
#define PANE_MODEL_MAC_BCRYPT_BCRYPT_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Hashes `pass` with 2^`log_rounds` rounds and a random salt into `hash`
// ("$2b$…", 61 bytes with its terminator). 0 on success, -1 on failure.
int bcrypt_newhash(const char* pass,
                   int log_rounds,
                   char* hash,
                   size_t hashlen);

// 0 when `pass` matches `goodhash` ("$2a$" or "$2b$"), -1 otherwise.
int bcrypt_checkpass(const char* pass, const char* goodhash);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // PANE_MODEL_MAC_BCRYPT_BCRYPT_H_
