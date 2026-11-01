// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// Compare os4/common/ml_printf.c (the OS4 freestanding printf) with the
// host C library for the formats the speech core and Flite use.
#include <stdio.h>
#include <string.h>
int ml_snprintf(char *buf, size_t size, const char *f, ...);

static int fails;
#define T(...) do { char a[256], b[256]; snprintf(a, sizeof a, __VA_ARGS__); \
    ml_snprintf(b, sizeof b, __VA_ARGS__); if (strcmp(a, b)) { fails++; \
    printf("FAIL \"%s\" vs \"%s\"\n", b, a); } } while (0)

int main(void)
{
    T("%.*s#%s#%.*s", 3, "00000000", "hello", 3, "00000000");
    T("%.*s%.*s%s", 4, "abcdefgh", 4, "ijklmnop", "x");
    T("%s-%s", "aa", "bb");
    T("%d %ld %u %x", -42, 123456789L, 7u, 255u);
    T("%5d|%-3s|%05d", 42, "a", 42);
    T("%c%c", 'a', 'b');
    T("%.10s_-_%.10s", "averyverylongname", "b");
    T("%*d", 6, 12);
    printf("ml_printf: %s\n", fails ? "FAIL" : "ok");
    return fails != 0;
}
