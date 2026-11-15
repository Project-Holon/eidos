/*
 * tests/test_ra.c -- RetroAchievements (sdk/ra_hash.c, sdk/ra_http.c).
 *
 *   - the hash worked out from the extracted parts equals rcheevos's own
 *     hash of the whole synthetic ROM they came from;
 *   - a short banner is zero-padded the way rcheevos pads it;
 *   - a header that claims more code than the parts hold is refused;
 *   - the HTTPS client refuses a non-https address.
 *
 * With EIDOS_RA_ONLINE=1 in the environment (and TLS in the build) it also
 * makes one real request to retroachievements.org, which checks the root
 * certificates in sdk/ra_roots.c against the live chain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ra_hash.h"
#include "ra_http.h"
#include "rc_consoles.h"
#include "rc_hash.h"

static int g_fail;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static void put32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

enum { A9 = 5000, A7 = 3000, BN = 0x840 };

int main(void)
{
    static unsigned char rom[0x10000];
    unsigned char header[0x200], arm9[A9], arm7[A7], banner[BN];
    char mine[33], theirs[33];
    unsigned i;
    FILE *f;
    BwRaHttpResult res;
    const char *tmp = getenv("TMPDIR");
    char path[512];

    /* A synthetic cartridge: header at 0, arm9 at 0x1000, arm7 at 0x4000,
     * banner at 0x8000. */
    memset(rom, 0, sizeof(rom));
    for (i = 0; i < A9; i++) arm9[i] = (unsigned char)(i * 7 + 1);
    for (i = 0; i < A7; i++) arm7[i] = (unsigned char)(i * 13 + 5);
    for (i = 0; i < BN; i++) banner[i] = (unsigned char)(i * 3 + 2);
    memset(header, 0, sizeof(header));
    memcpy(header, "SYNTHETICGAME", 13);
    put32(header + 0x20, 0x1000); put32(header + 0x2C, A9);
    put32(header + 0x30, 0x4000); put32(header + 0x3C, A7);
    put32(header + 0x68, 0x8000);
    for (i = 0x80; i < 0x200; i++) header[i] = (unsigned char)i;
    memcpy(rom, header, 0x200);
    memcpy(rom + 0x1000, arm9, A9);
    memcpy(rom + 0x4000, arm7, A7);
    memcpy(rom + 0x8000, banner, BN);       /* the file ends 0x1C0 past the banner: padded by zeros */

    snprintf(path, sizeof(path), "%s/eidos_test_ra.nds", tmp != NULL ? tmp : "/tmp");
    f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f == NULL) return 1;
    fwrite(rom, 1, sizeof(rom), f);
    fclose(f);

    memset(theirs, 0, sizeof(theirs));
    CHECK(rc_hash_generate_from_file(theirs, RC_CONSOLE_NINTENDO_DS, path) != 0);
    CHECK(bw_ra_hash_parts(header, sizeof(header), arm9, A9, arm7, A7, banner, BN, mine) == 0);
    printf("rcheevos %s\nours     %s\n", theirs, mine);
    CHECK(strcmp(mine, theirs) == 0);

    /* A banner cut short at the end of the file is padded: the same file
     * truncated right after the banner. */
    f = fopen(path, "wb");
    fwrite(rom, 1, 0x8000 + BN, f);
    fclose(f);
    memset(theirs, 0, sizeof(theirs));
    CHECK(rc_hash_generate_from_file(theirs, RC_CONSOLE_NINTENDO_DS, path) != 0);
    CHECK(strcmp(mine, theirs) == 0);
    remove(path);

    /* A header that claims more than the parts hold. */
    CHECK(bw_ra_hash_parts(header, sizeof(header), arm9, A9 - 1, arm7, A7, banner, BN, mine) == -1);
    CHECK(bw_ra_hash_parts(header, 0x100, arm9, A9, arm7, A7, banner, BN, mine) == -1);

    /* A missing folder is a sentence, not a crash. */
    {
        char err[160] = "";

        CHECK(bw_ra_hash_from_root("/nonexistent-eidos-folder", mine, err, sizeof(err)) == -1);
        CHECK(strstr(err, "header.bin") != NULL);
    }

    CHECK(bw_ra_http_request("http://retroachievements.org/", NULL, NULL, NULL, 0, 5000, &res) == -1);
    CHECK(res.error[0] != '\0');

    if (getenv("EIDOS_RA_ONLINE") != NULL) {
        int rc = bw_ra_http_request("https://retroachievements.org/dorequest.php",
                                    "eidos-test", NULL, "r=gameid&m=00000000000000000000000000000000",
                                    43, 20000, &res);

        printf("online: rc=%d status=%d error='%s' body=%.80s\n", rc, res.status, res.error,
               res.body != NULL ? res.body : "");
        CHECK(rc == 0);
        CHECK(res.status == 200);
        free(res.body);
    }

    printf(g_fail ? "test_ra: %d FAILED\n" : "test_ra: ok\n", g_fail);
    return g_fail ? 1 : 0;
}
