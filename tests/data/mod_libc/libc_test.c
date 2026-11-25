/* Tests of the mod C library, run in Node (run.mjs). Each failure is one
 * log line; the last line is the count. */
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "eidos_wasm.h"

static int failures;
static int checks;

static void say(const char *s)
{
    eidos_log(s, (uint32_t)strlen(s));
}

static void fmt_case(const char *want, int want_n, const char *got, int got_n, const char *what)
{
    char line[300];
    checks++;
    if (strcmp(want, got) != 0 || want_n != got_n) {
        failures++;
        snprintf(line, sizeof line, "FAIL %s: want [%s] %d got [%s] %d", what, want, want_n, got,
                 got_n);
        say(line);
    }
}

#define F(want, ...)                                                       \
    do {                                                                   \
        char b_[200];                                                      \
        int n_ = snprintf(b_, sizeof b_, __VA_ARGS__);                     \
        fmt_case(want, (int)strlen(want), b_, n_, #__VA_ARGS__);           \
    } while (0)

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        failures++;
        say(what);
    }
}

static void test_printf(void)
{
    char small[5];
    int n;
    F("42", "%d", 42);
    F("-42", "%d", -42);
    F("+42", "%+d", 42);
    F(" 42", "% d", 42);
    F("   42", "%5d", 42);
    F("42   |", "%-5d|", 42);
    F("00042", "%05d", 42);
    F("-0042", "%05d", -42);
    F("  042", "%5.3d", 42);
    F("", "%.0d", 0);
    F("4294967295", "%u", 4294967295u);
    F("ff", "%x", 255);
    F("FF", "%X", 255);
    F("0xff", "%#x", 255);
    F("0", "%#x", 0);
    F("017", "%#o", 15);
    F("0a", "%02x", 10);
    F("00AB12", "%06X", 0xab12);
    F("-2147483648", "%d", (int)0x80000000u);
    F("18446744073709551615", "%llu", 18446744073709551615ull);
    F("-9223372036854775808", "%lld", (long long)0x8000000000000000ull);
    F("123456", "%zu", (size_t)123456);
    F("4000000000", "%lu", 4000000000ul);
    F("x", "%c", 'x');
    F("abc", "%s", "abc");
    F("ab", "%.2s", "abc");
    F("  abc", "%5s", "abc");
    F("abc  |", "%-5s|", "abc");
    F("   ab", "%*.*s", 5, 2, "abc");
    F("100%", "%d%%", 100);
    F("0x00000010", "%p", (void *)16); /* eyalroz/printf pads %p to the pointer's width */
    F("3.140000", "%f", 3.14);
    F("3.14", "%.2f", 3.14159);
    F("2.6", "%.1f", 2.65); /* 2.65 is 2.6499999... in binary, as glibc prints */
    F("0.50", "%.2f", 0.499999);
    F("1.00", "%.2f", 0.999);
    F("-0.5", "%.1f", -0.5);
    F("12", "%.0f", 12.4);
    F("1e+06", "%g", 1000000.0);
    F("123456", "%g", 123456.0);
    F("0.0001", "%g", 0.0001);
    F("1e-05", "%g", 0.00001);
    F("1.5", "%g", 1.5);
    F("1.000000e+10", "%e", 1e10);
    F("2.50E-03", "%.2E", 0.0025);
    F(" 1.000000", "% f", 1.0);
    F("     1.50", "%9.2f", 1.5);
    F("-00001.50", "%09.2f", -1.5);
    F("inf", "%f", __builtin_inf());
    F("nan", "%f", __builtin_nan(""));
    F("12.3457", "%.4f", 12.345678);
    F("0.000001", "%.6f", 0.000001);
    F("100.000000", "%f", 100.0);
    F("2e+18", "%f", 1844674407370955161.6); /* %f of 1e9 or more turns to %e (printf's MAX_INTEGRAL_DIGITS_FOR_DECIMAL 9) */
    F("999999999.500000", "%f", 999999999.5);
    n = snprintf(small, sizeof small, "%s", "hello world");
    check(n == 11 && strcmp(small, "hell") == 0, "FAIL snprintf truncation");
    n = snprintf(NULL, 0, "%d", 12345);
    check(n == 5, "FAIL snprintf size 0");
}

static void test_strings(void)
{
    char buf[32];
    check(strlen("abc") == 3, "FAIL strlen");
    check(strcmp("abc", "abd") < 0 && strcmp("b", "a") > 0 && strcmp("x", "x") == 0, "FAIL strcmp");
    check(strncmp("abcdef", "abcxyz", 3) == 0 && strncmp("ab", "abc", 3) < 0, "FAIL strncmp");
    check(strcasecmp("HeLLo", "hello") == 0, "FAIL strcasecmp");
    check(strstr("hello world", "o w") == (char *)"hello world" + 4 || strcmp(strstr("hello world", "o w"), "o world") == 0, "FAIL strstr");
    check(strstr("abc", "") != NULL && strstr("abc", "d") == NULL, "FAIL strstr edge");
    check(strchr("abc", 'c') && *strchr("abc", 'c') == 'c' && strchr("abc", '\0') && strrchr("abca", 'a')[1] == '\0', "FAIL strchr");
    strcpy(buf, "ab");
    strcat(buf, "cd");
    strncat(buf, "efgh", 2);
    check(strcmp(buf, "abcdef") == 0, "FAIL strcat");
    memmove(buf + 1, buf, 6);
    check(memcmp(buf, "aabcdef", 7) == 0, "FAIL memmove");
    check(strspn("aab1", "ab") == 3 && strcspn("ab1", "1") == 2, "FAIL strspn");
    {
        char *d = strdup("copy");
        check(d && strcmp(d, "copy") == 0, "FAIL strdup");
        free(d);
    }
    check(isalpha('a') && !isalpha('1') && isdigit('7') && isspace('\n') && tolower('Q') == 'q', "FAIL ctype");
}

static void test_numbers(void)
{
    char *end;
    check(strtol("  -123xyz", &end, 10) == -123 && *end == 'x', "FAIL strtol");
    check(strtol("0x1F", NULL, 0) == 31 && strtol("017", NULL, 0) == 15, "FAIL strtol base 0");
    check(strtoul("4294967295", NULL, 10) == 4294967295ul, "FAIL strtoul max");
    errno = 0;
    check(strtoul("99999999999", NULL, 10) == 4294967295ul && errno == ERANGE, "FAIL strtoul over");
    errno = 0;
    check(strtol("2147483648", NULL, 10) == 2147483647L && errno == ERANGE, "FAIL strtol over");
    check(strtol("-2147483648", NULL, 10) == (long)-2147483647L - 1, "FAIL strtol min");
    check(strtoull("18446744073709551615", NULL, 10) == 18446744073709551615ull, "FAIL strtoull");
    check(strtol("abc", &end, 10) == 0 && strcmp(end, "abc") == 0, "FAIL strtol none");
    check(atoi("42") == 42 && atoi("  -7") == -7, "FAIL atoi");
}

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a;
    int y = *(const int *)b;
    return (x > y) - (x < y);
}

static void test_sort(void)
{
    int v[200];
    int i;
    int key = 77;
    unsigned s = 12345;
    for (i = 0; i < 200; i++) {
        s = s * 1103515245u + 12345u;
        v[i] = (int)(s >> 16) % 100;
    }
    qsort(v, 200, sizeof v[0], cmp_int);
    for (i = 1; i < 200; i++) {
        if (v[i - 1] > v[i]) {
            break;
        }
    }
    check(i == 200, "FAIL qsort order");
    for (i = 0; i < 200; i++) {
        v[i] = i * 2;
    }
    check(bsearch(&(int){78}, v, 200, sizeof v[0], cmp_int) == &v[39], "FAIL bsearch hit");
    check(bsearch(&key, v, 200, sizeof v[0], cmp_int) == NULL, "FAIL bsearch miss");
}

/* Random allocations, frees and reallocs; each block holds a pattern that
 * must survive. */
static void test_malloc(void)
{
    enum { N = 300 };
    unsigned char *p[N];
    size_t n[N];
    unsigned s = 99;
    int round;
    int i;
    int bad = 0;
    memset(p, 0, sizeof p);
    for (round = 0; round < 20000; round++) {
        s = s * 1103515245u + 12345u;
        i = (int)((s >> 8) % N);
        if (p[i]) {
            size_t k;
            for (k = 0; k < n[i]; k++) {
                if (p[i][k] != (unsigned char)(i + k)) {
                    bad++;
                    break;
                }
            }
            if ((s >> 20) & 1) {
                size_t m = ((s >> 4) % 3000) + 1;
                unsigned char *q = realloc(p[i], m);
                if (q) {
                    size_t keep = m < n[i] ? m : n[i];
                    for (k = 0; k < keep; k++) {
                        if (q[k] != (unsigned char)(i + k)) {
                            bad++;
                            break;
                        }
                    }
                    for (k = 0; k < m; k++) {
                        q[k] = (unsigned char)(i + k);
                    }
                    p[i] = q;
                    n[i] = m;
                }
            } else {
                free(p[i]);
                p[i] = NULL;
            }
        } else {
            size_t m = ((s >> 4) % 3000) + 1;
            size_t k;
            p[i] = malloc(m);
            if (p[i]) {
                n[i] = m;
                for (k = 0; k < m; k++) {
                    p[i][k] = (unsigned char)(i + k);
                }
            }
        }
    }
    for (i = 0; i < N; i++) {
        free(p[i]);
    }
    check(bad == 0, "FAIL malloc pattern");
    /* after freeing all, one big block must fit again (no leak, joined) */
    {
        void *big = malloc(900 * 1024);
        check(big != NULL, "FAIL malloc rejoin");
        free(big);
    }
    check(malloc(2 * 1024 * 1024) == NULL, "FAIL malloc too big");
    {
        int *z = calloc(100, sizeof(int));
        int all = 1;
        for (i = 0; i < 100; i++) {
            all &= z[i] == 0;
        }
        check(z && all, "FAIL calloc");
        free(z);
    }
}

static void test_time(void)
{
    struct tm tm;
    time_t t = 951782400; /* 2000-02-29 00:00:00 UTC */
    gmtime_r(&t, &tm);
    check(tm.tm_year == 100 && tm.tm_mon == 1 && tm.tm_mday == 29 && tm.tm_wday == 2 && tm.tm_yday == 59, "FAIL gmtime leap day");
    t = 1732492800 + 3661; /* 2024-11-25 01:01:01 */
    gmtime_r(&t, &tm);
    check(tm.tm_hour == 1 && tm.tm_min == 1 && tm.tm_sec == 1 && tm.tm_mday == 25 && tm.tm_mon == 10, "FAIL gmtime time");
    check(timegm(&tm) == t, "FAIL timegm");
    tm.tm_mday += 10; /* out of range: Dec 5 */
    timegm(&tm);
    check(tm.tm_mon == 11 && tm.tm_mday == 5, "FAIL timegm normalise");
    t = -1;
    gmtime_r(&t, &tm);
    check(tm.tm_year == 69 && tm.tm_hour == 23 && tm.tm_sec == 59, "FAIL gmtime before 1970");
}

static void test_math(void)
{
    check(floor(-1.5) == -2.0 && ceil(1.2) == 2.0 && trunc(-1.7) == -1.0, "FAIL floor");
    check(fmod(10.0, 3.0) == 1.0 && fmod(-10.0, 3.0) == -1.0 && fmod(5.5, 2.0) == 1.5, "FAIL fmod");
    check(fmod(1e300, 3.0) == 1.0 || fmod(1e300, 3.0) == 2.0 || fmod(1e300, 3.0) == 0.0, "FAIL fmod big");
    check(sqrt(16.0) == 4.0, "FAIL sqrt");
}

EIDOS_EXPORT("eidos_mod_start")
int32_t eidos_mod_start(void)
{
    char line[64];
    test_printf();
    test_strings();
    test_numbers();
    test_sort();
    test_malloc();
    test_time();
    test_math();
    snprintf(line, sizeof line, "%d checks, %d failures", checks, failures);
    say(line);
    return failures;
}

EIDOS_EXPORT("eidos_mod_stop")
void eidos_mod_stop(void)
{
}
