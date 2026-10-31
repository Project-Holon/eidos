/*
 * tests/test_armdata.c -- the ARM9 data runtime (sdk/armdata.c) against a
 * synthetic image and hand-written tables.  No game data: the image is a
 * byte pattern made here, and every expected value follows from it.
 *
 * What is checked: the copy of each initialised range into the DS address
 * space and the zeroing of each .bss range; the three bindings (an address in
 * the copy, a linker constant, a port object); two names for one address;
 * a name whose address a registered object owns resolving to that object;
 * every pointer word rewritten (a data target, a Thumb code target, a port
 * object) on an ILP32 host and refused, untouched, on an LP64 one; a word
 * that does not hold what the tables say; a missing image; and the reset
 * path that runs it all from bw_ds_reset.
 *
 * `test_armdata --trap` calls the trap the generator emits for code the
 * build lacks; tests/expect_panic.cmake checks the panic (test_armdata_trap).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "port.h"
#include "dsmem.h"
#include "armdata.h"
#include "port_test.h"

#define CHECK_PTR(label, got, want)                                        \
    do {                                                                   \
        bw_checks++;                                                       \
        if ((const void *)(got) != (const void *)(want)) {                 \
            bw_failures++;                                                 \
            printf("  FAIL %-40s got %p, want %p\n", (label),              \
                   (const void *)(got), (const void *)(want));             \
        }                                                                  \
    } while (0)

/* The registered port object the bindings must resolve to (game_globals.c). */
extern char g_Sink[];

/* The synthetic image: 0x200 bytes "loaded" at 0x02004000. */
#define IMG_BASE 0x02004000u
#define IMG_SIZE 0x200u
static u8 g_image[IMG_SIZE];

/* A port object at a DS address, as the exclusions list would name one. */
static u8 g_port_obj[16];
#define PORT_ADDR 0x02300000u

static int g_fn_called;
static void test_fn(void)
{
    g_fn_called++;
}

static const BwDataRange k_ranges[] = {
    { 0x02004000u, 0x100u, 0x02004000u, 1 },   /* the static module      */
    { 0x02FE0000u, 0x020u, 0x02004100u, 1 },   /* an autoload block      */
    { 0x02010000u, 0x040u, 0, 0 },             /* .bss                   */
    { 0x01FF8000u, 0x020u, 0x02004180u, 1 },   /* ITCM: skipped          */
};

static void *bw_dsref_t_data;
static void *bw_dsref_t_alias;
static void *bw_dsref_t_const;
static void *bw_dsref_t_host;
static void *bw_dsref_t_sink;
static void *bw_dsref_t_bss;
static void *bw_dsref_t_zero;

/* A name that could not be a macro: a host object of its own. */
static BwDataCell t_obj[BW_DATA_CELLS(0x10u)];

static const BwDataName k_names[] = {
    { "t_data",  0x02004010u, &bw_dsref_t_data,  BW_DATA_BIND_DS,    BW_DATA_SEC_DATA,  4, 8, NULL, 0 },
    { "t_alias", 0x02004010u, &bw_dsref_t_alias, BW_DATA_BIND_DS,    BW_DATA_SEC_DATA,  0, 0, NULL, 0 },
    { "t_const", 0x00000800u, &bw_dsref_t_const, BW_DATA_BIND_CONST, BW_DATA_SEC_CONST, 0, 0, NULL, 0 },
    { "t_host",  0x02300004u, &bw_dsref_t_host,  BW_DATA_BIND_HOST,  BW_DATA_SEC_BSS,   0, 0, g_port_obj, PORT_ADDR },
    { "t_sink",  0x0214699Cu, &bw_dsref_t_sink,  BW_DATA_BIND_DS,    BW_DATA_SEC_BSS,   0, 0, NULL, 0 },
    { "t_bss",   0x02010010u, &bw_dsref_t_bss,   BW_DATA_BIND_DS,    BW_DATA_SEC_BSS,   0, 0x30, NULL, 0 },
    { "t_obj",   0x02004040u, NULL,              BW_DATA_BIND_OBJECT, BW_DATA_SEC_DATA, 0, 0x10, t_obj, 0x02004040u },
    /* a linker constant 0 (ov029's abs_id_0, overlay 0) */
    { "t_zero",  0x00000000u, &bw_dsref_t_zero,  BW_DATA_BIND_CONST, BW_DATA_SEC_CONST, 0, 0, NULL, 0 },
};

static const BwDataObject k_objects[] = {
    { 0x02004040u, 0x10u, t_obj, "t_obj" },
};

static const BwDataFunc k_funcs[] = {
    { 0x02012345u, test_fn, "test_fn" },
};

static const BwDataAnchor k_anchors[] = {
    { PORT_ADDR, sizeof(g_port_obj), g_port_obj, "g_port_obj" },
};

static const BwDataReloc k_relocs[] = {
    { 0x02004020u, 0x02004080u, BW_DATA_RELOC_DATA, 0 },
    { 0x02004024u, 0x02012345u, BW_DATA_RELOC_CODE, 0 },
    { 0x02004028u, 0x02300004u, BW_DATA_RELOC_HOST, 0 },
    { 0x02FE0004u, 0x02004000u, BW_DATA_RELOC_DATA, 0 },
    { 0x02004044u, 0x02004080u, BW_DATA_RELOC_DATA, 0 },   /* inside t_obj */
};

/* Where bw_data_relocate also looks for code pointers the tables do not
 * list (docs/armdata.md, "Code pointers the metadata does not list"). */
static const BwDataRange k_scan[] = {
    { 0x02004030u, 0xD0u, 0, 0 },
};

static const BwDataImage k_img = {
    "arm9", -1,
    k_ranges, 4,
    k_names, 8,
    k_relocs, 5,
    k_funcs, 1,
    k_anchors, 1,
    k_objects, 1,
    k_scan, 1
};

static void make_image(void)
{
    u32 i;

    for (i = 0; i < IMG_SIZE; i++) {
        g_image[i] = (u8)(i * 7u + 3u);
    }
    /* The pointer words hold what the tables say they hold. */
    bw_wr32(&g_image[0x20], 0x02004080u);
    bw_wr32(&g_image[0x24], 0x02012345u);
    bw_wr32(&g_image[0x28], 0x02300004u);
    bw_wr32(&g_image[0x104], 0x02004000u);   /* autoload +4 */
    bw_wr32(&g_image[0x44], 0x02004080u);    /* in t_obj */
    bw_wr32(&g_image[0x30], 0x02012345u);    /* unlisted: the scan finds it */
    bw_wr32(&g_image[0x34], 0x02012344u);    /* not an entry: left alone */
    bw_arm9_set_image(g_image, IMG_SIZE);
}

static u32 word_at(u32 addr)
{
    return bw_rd32((const u8 *)bw_ds_ptr(addr));
}

static void check_relocated(const char *when)
{
    char label[96];

    if (bw_data_relocation_supported()) {
        snprintf(label, sizeof(label), "%s: data pointer", when);
        BW_CHECK_EQ(label, word_at(0x02004020u),
                    (u32)(uintptr_t)bw_ds_ptr(0x02004080u));
        snprintf(label, sizeof(label), "%s: code pointer", when);
        BW_CHECK_EQ(label, word_at(0x02004024u), (u32)(uintptr_t)test_fn);
        snprintf(label, sizeof(label), "%s: port-object pointer", when);
        BW_CHECK_EQ(label, word_at(0x02004028u),
                    (u32)(uintptr_t)(g_port_obj + 4));
        snprintf(label, sizeof(label), "%s: pointer inside a host object", when);
        BW_CHECK_EQ(label, bw_rd32((const u8 *)t_obj + 4),
                    (u32)(uintptr_t)bw_ds_ptr(0x02004080u));
        snprintf(label, sizeof(label), "%s: unlisted code pointer", when);
        BW_CHECK_EQ(label, word_at(0x02004030u), (u32)(uintptr_t)test_fn);
        snprintf(label, sizeof(label), "%s: a number next to it", when);
        BW_CHECK_EQ(label, word_at(0x02004034u), 0x02012344u);
        snprintf(label, sizeof(label), "%s: autoload pointer", when);
        BW_CHECK_EQ(label, word_at(0x02FE0004u),
                    (u32)(uintptr_t)bw_ds_ptr(0x02004000u));
    } else {
        snprintf(label, sizeof(label), "%s: LP64 leaves the DS address", when);
        BW_CHECK_EQ(label, word_at(0x02004020u), 0x02004080u);
        BW_CHECK_EQ(label, word_at(0x02004024u), 0x02012345u);
    }
}

static int test_steps(void)
{
    int rc;

    make_image();
    bw_data_install(NULL, 0);
    bw_ds_reset();

    /* Dirty the .bss range so the zeroing is visible. */
    memset(bw_ds_ptr(0x02010000u), 0xAA, 0x40);

    rc = bw_data_load(&k_img);
    BW_CHECK_EQ("load", rc, BW_DATA_OK);
    BW_CHECK_EQ("static byte 0x00", *(u8 *)bw_ds_ptr(0x02004000u), g_image[0x00]);
    BW_CHECK_EQ("static byte 0xFF", *(u8 *)bw_ds_ptr(0x020040FFu), g_image[0xFF]);
    BW_CHECK_EQ("past the range untouched", *(u8 *)bw_ds_ptr(0x02004100u), 0);
    BW_CHECK_EQ("autoload byte 0x00", *(u8 *)bw_ds_ptr(0x02FE0000u), g_image[0x100]);
    BW_CHECK_EQ("autoload byte 0x1F", *(u8 *)bw_ds_ptr(0x02FE001Fu), g_image[0x11F]);
    BW_CHECK_EQ(".bss zeroed", *(u8 *)bw_ds_ptr(0x0201003Fu), 0);
    BW_REQUIRE("ITCM skipped", bw_ds_try_ptr(0x01FF8000u) == NULL);
    CHECK_PTR("host object registered", bw_ds_ptr(0x02004048u), (u8 *)t_obj + 8);
    BW_CHECK_EQ("host object filled", *((u8 *)t_obj + 0xF), g_image[0x4F]);

    bw_dsref_t_zero = &bw_dsref_t_zero;
    rc = bw_data_bind(&k_img);
    BW_CHECK_EQ("bind", rc, BW_DATA_OK);
    CHECK_PTR("t_zero is the value 0", bw_dsref_t_zero, NULL);
    CHECK_PTR("t_data -> its DS address", bw_dsref_t_data, bw_ds_ptr(0x02004010u));
    CHECK_PTR("t_alias -> the same byte", bw_dsref_t_alias, bw_dsref_t_data);
    CHECK_PTR("t_const is the value", bw_dsref_t_const, (void *)(uintptr_t)0x800u);
    CHECK_PTR("t_host -> port object + 4", bw_dsref_t_host, g_port_obj + 4);
    CHECK_PTR("t_sink -> the registered object", bw_dsref_t_sink, g_Sink);
    CHECK_PTR("t_bss -> its DS address", bw_dsref_t_bss, bw_ds_ptr(0x02010010u));
    BW_CHECK_EQ("t_data reads the image", *(u8 *)bw_dsref_t_data, g_image[0x10]);

    rc = bw_data_relocate(&k_img);
    if (bw_data_relocation_supported()) {
        BW_CHECK_EQ("relocate", rc, BW_DATA_OK);
    } else {
        BW_CHECK_EQ("relocate refuses on LP64", rc, BW_DATA_E_LP64);
        BW_REQUIRE("LP64 message", strstr(bw_data_error(), "LP64") != NULL);
    }
    check_relocated("steps");

    /* The relocated code pointer calls the function. */
    if (bw_data_relocation_supported()) {
        u32 w = word_at(0x02004024u);
        BwDataFn fn = (BwDataFn)(uintptr_t)w;
        fn();
        BW_CHECK_EQ("relocated function called", g_fn_called, 1);
    }

    /* A second relocation finds host pointers, not DS addresses. */
    if (bw_data_relocation_supported()) {
        rc = bw_data_relocate(&k_img);
        BW_CHECK_EQ("relocating twice is a mismatch", rc, BW_DATA_E_MISMATCH);
    }
    return 0;
}

static int test_mismatch_and_missing(void)
{
    int rc;

    make_image();
    bw_wr32(&g_image[0x24], 0x02012349u);       /* another build's pointer */
    bw_arm9_set_image(g_image, IMG_SIZE);
    bw_ds_reset();
    rc = bw_data_load(&k_img);
    BW_CHECK_EQ("load (mismatching image)", rc, BW_DATA_OK);
    rc = bw_data_relocate(&k_img);
    BW_CHECK_EQ("mismatch detected", rc, BW_DATA_E_MISMATCH);
    BW_REQUIRE("mismatch names the word",
               strstr(bw_data_error(), "0x02004024") != NULL);
    BW_CHECK_EQ("nothing written on a mismatch", word_at(0x02004020u), 0x02004080u);

    bw_arm9_set_image(NULL, 0);
    bw_ds_reset();
    rc = bw_data_load(&k_img);
    BW_CHECK_EQ("missing image", rc, BW_DATA_E_NOIMAGE);
    return 0;
}

static int test_reset_path(void)
{
    make_image();
    bw_data_install(&k_img, BW_DATA_ALLOW_UNRELOCATED);
    BW_REQUIRE("installed", bw_data_installed() == &k_img);
    bw_ds_reset();
    CHECK_PTR("reset: t_data bound", bw_dsref_t_data, bw_ds_ptr(0x02004010u));
    CHECK_PTR("reset: t_sink -> registered object", bw_dsref_t_sink, g_Sink);
    /* 0x0200400F: an image byte outside every relocated or scanned word (the
     * code-pointer scan may rewrite words from 0x02004030 at ILP32).        */
    BW_CHECK_EQ("reset: image copied", *(u8 *)bw_ds_ptr(0x0200400Fu), g_image[0x0F]);
    check_relocated("reset");

    /* A second reset rebuilds the same state from the image. */
    *(u8 *)bw_ds_ptr(0x0200400Fu) = 0;
    bw_ds_reset();
    BW_CHECK_EQ("second reset: image copied again",
                *(u8 *)bw_ds_ptr(0x0200400Fu), g_image[0x0F]);
    check_relocated("second reset");

    BW_REQUIRE("find", bw_data_find(&k_img, "t_bss") == &k_names[5]);
    CHECK_PTR("reset: host object registered", bw_ds_ptr(0x02004040u), t_obj);
    BW_REQUIRE("find missing", bw_data_find(&k_img, "nope") == NULL);

    bw_data_install(NULL, 0);
    bw_arm9_set_image(NULL, 0);
    bw_ds_reset();
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--trap") == 0) {
        bw_ds_reset();
        bw_data_trap(0x02012344u);
        return 0;
    }
    printf("pointer width %u, relocation %s\n", (unsigned)sizeof(void *),
           bw_data_relocation_supported() ? "supported" : "refused (LP64)");
    if (test_steps() != 0 || test_mismatch_and_missing() != 0
        || test_reset_path() != 0) {
        BW_REPORT("test_armdata");
    }
    BW_REPORT("test_armdata");
}
