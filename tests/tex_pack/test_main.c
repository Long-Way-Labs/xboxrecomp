/*
 * tex_pack: the texture pack reader (nv2a_tex_pack.h).
 *
 *   write     a pack of two entries (a 2x2 single level, a 4x2 with two
 *             levels) written by this test, loaded: count, scale, both
 *             found, a miss, an entry of another aspect than the guest
 *             texture's not taken, texels read back, hits counted
 *   refuse    bad magic, bad version, index past the end, an entry past
 *             the end, an unsorted index, a truncated file, more levels
 *             than the size allows, non-zero reserved header bytes or entry
 *             flags, a missing file: each refused with a reason, and nothing
 *             loaded; the most levels the size allows still loads
 *   hash      nv2a_tex_hash of a fixed byte pattern (1034 bytes: 0..255 four
 *             times, then "xboxrecomp") is 0x0B060D91FB09F186, the vector
 *             the CLI's Python port (xboxrecomp_cli.enhance.texhash) checks;
 *             the empty and the 64-zero-byte cases too
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nv2a_backend_common.h"
#include "nv2a_tex_pack.h"

static int fails;

#define CHECK(cond, ...) do { \
    if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void wr64(uint8_t *p, uint64_t v)
{
    wr32(p, (uint32_t)v);
    wr32(p + 4, (uint32_t)(v >> 32));
}

#define HASH_A 0x1111111111111111ull
#define HASH_B 0x2222222222222222ull
#define TEXELS_A 4u                 /* 2x2, one level */
#define TEXELS_B (8u + 2u)          /* 4x2 then 2x1 */

/* The pack image: header, index, payload A, payload B. */
static size_t build_pack(uint8_t *buf, uint64_t hash_a, uint64_t hash_b, uint32_t version)
{
    size_t idx = NV2A_TEX_PACK_HDR, pay_a = idx + 2 * NV2A_TEX_PACK_ENTRY;
    size_t pay_b = pay_a + TEXELS_A * 4, end = pay_b + TEXELS_B * 4;
    uint32_t i;
    memset(buf, 0, end);
    memcpy(buf, NV2A_TEX_PACK_MAGIC, 4);
    wr32(buf + 4, version);
    wr32(buf + 8, 2);
    wr32(buf + 12, 4);
    wr64(buf + 16, idx);
    wr64(buf + idx, hash_a);
    wr32(buf + idx + 8, 2);
    wr32(buf + idx + 12, 2);
    wr32(buf + idx + 16, 1);
    wr64(buf + idx + 24, pay_a);
    wr64(buf + idx + 32, hash_b);
    wr32(buf + idx + 40, 4);
    wr32(buf + idx + 44, 2);
    wr32(buf + idx + 48, 2);
    wr64(buf + idx + 56, pay_b);
    for (i = 0; i < TEXELS_A; i++)
        wr32(buf + pay_a + i * 4, 0xFF000000u | (i * 0x010101u));
    for (i = 0; i < TEXELS_B; i++)
        wr32(buf + pay_b + i * 4, 0x80000000u | (0x100u * i));
    return end;
}

static void write_file(const char *path, const uint8_t *buf, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) { fails++; printf("FAIL cannot write %s\n", path); return; }
    fwrite(buf, 1, n, f);
    fclose(f);
}

static void test_write(void)
{
    uint8_t buf[512];
    char err[128] = "";
    size_t n = build_pack(buf, HASH_A, HASH_B, NV2A_TEX_PACK_VERSION);
    struct nv2a_tex_pack_entry *e;
    const uint32_t *px;
    int rc;

    write_file("good.pack", buf, n);
    rc = nv2a_tex_pack_load("good.pack", err, sizeof err);
    CHECK(rc == 2, "load: %d (%s)", rc, err);
    CHECK(nv2a_tex_pack_count() == 2, "count %u", nv2a_tex_pack_count());
    CHECK(nv2a_tex_pack_scale() == 4, "scale %u", nv2a_tex_pack_scale());
    CHECK(nv2a_tex_pack_find(0x3333333333333333ull, 1, 1) == NULL, "miss found something");
    CHECK(nv2a_tex_pack_hits() == 0, "a miss counted as a hit");
    e = nv2a_tex_pack_find(HASH_A, 1, 1);         /* a 1x1 guest at 2x */
    CHECK(e && e->width == 2 && e->height == 2 && e->levels == 1 && e->texels == TEXELS_A,
          "entry A");
    px = nv2a_tex_pack_texels(e);
    CHECK(px && px[0] == 0xFF000000u && px[3] == 0xFF030303u, "texels A");
    CHECK(px == nv2a_tex_pack_texels(e), "texels A read twice");
    /* B is 4x2: a square guest texture of the same hash is something else. */
    CHECK(nv2a_tex_pack_find(HASH_B, 2, 2) == NULL, "entry B taken for a square guest");
    CHECK(nv2a_tex_pack_hits() == 1, "an aspect mismatch counted as a hit");
    e = nv2a_tex_pack_find(HASH_B, 2, 1);
    CHECK(e && e->width == 4 && e->height == 2 && e->levels == 2 && e->texels == TEXELS_B,
          "entry B");
    px = nv2a_tex_pack_texels(e);
    CHECK(px && px[0] == 0x80000000u && px[9] == 0x80000900u, "texels B");
    CHECK(nv2a_tex_pack_hits() == 2, "hits %llu", (unsigned long long)nv2a_tex_pack_hits());
    nv2a_tex_pack_unload();
    CHECK(nv2a_tex_pack_count() == 0, "unload");
}

static void refuse(const char *path, const uint8_t *buf, size_t n, const char *want)
{
    char err[128] = "";
    int rc;
    write_file(path, buf, n);
    rc = nv2a_tex_pack_load(path, err, sizeof err);
    CHECK(rc == -1, "%s: loaded (%d)", path, rc);
    CHECK(strstr(err, want) != NULL, "%s: reason '%s', want '%s'", path, err, want);
    CHECK(nv2a_tex_pack_count() == 0, "%s: count %u after refusal", path, nv2a_tex_pack_count());
}

static void test_refuse(void)
{
    uint8_t buf[512];
    char err[128] = "";
    size_t n;

    n = build_pack(buf, HASH_A, HASH_B, NV2A_TEX_PACK_VERSION);
    buf[0] = 'Y';
    refuse("magic.pack", buf, n, "magic");

    n = build_pack(buf, HASH_A, HASH_B, 7);
    refuse("version.pack", buf, n, "version");

    n = build_pack(buf, HASH_A, HASH_B, NV2A_TEX_PACK_VERSION);
    wr32(buf + 8, 100);
    refuse("index.pack", buf, n, "index outside");

    n = build_pack(buf, HASH_A, HASH_B, NV2A_TEX_PACK_VERSION);
    wr64(buf + NV2A_TEX_PACK_HDR + 56, n - 4);      /* B's texels past the end */
    refuse("entry.pack", buf, n, "outside the file");

    /* Offsets near 2^64: a sum would wrap to a small value inside the file. */
    n = build_pack(buf, HASH_A, HASH_B, NV2A_TEX_PACK_VERSION);
    wr64(buf + 16, ~0ull - 31);
    refuse("index_wrap.pack", buf, n, "index outside");

    n = build_pack(buf, HASH_A, HASH_B, NV2A_TEX_PACK_VERSION);
    wr64(buf + NV2A_TEX_PACK_HDR + 56, ~0ull - 3);
    refuse("entry_wrap.pack", buf, n, "outside the file");

    n = build_pack(buf, HASH_B, HASH_A, NV2A_TEX_PACK_VERSION);
    refuse("unsorted.pack", buf, n, "sorted");

    n = build_pack(buf, HASH_A, HASH_B, NV2A_TEX_PACK_VERSION);
    refuse("short.pack", buf, NV2A_TEX_PACK_HDR + 10, "index outside");
    refuse("stub.pack", buf, 8, "too short");

    n = build_pack(buf, HASH_A, HASH_B, NV2A_TEX_PACK_VERSION);
    wr32(buf + NV2A_TEX_PACK_HDR + 8, 0);            /* A's width 0 */
    refuse("size.pack", buf, n, "bad size");

    /* 2x2 allows two levels (2x2, 1x1); a third would be another 1x1,
     * which D3D11 refuses and Metal reports as invalid. */
    n = build_pack(buf, HASH_A, HASH_B, NV2A_TEX_PACK_VERSION);
    wr32(buf + NV2A_TEX_PACK_HDR + 16, 3);
    refuse("levels.pack", buf, n, "bad size");
    n = build_pack(buf, HASH_A, HASH_B, NV2A_TEX_PACK_VERSION);
    wr32(buf + NV2A_TEX_PACK_HDR + 16, 2);           /* A's 5 texels run into B's */
    write_file("levels_max.pack", buf, n);
    CHECK(nv2a_tex_pack_load("levels_max.pack", err, sizeof err) == 2,
          "levels at the limit refused: %s", err);
    nv2a_tex_pack_unload();

    n = build_pack(buf, HASH_A, HASH_B, NV2A_TEX_PACK_VERSION);
    wr64(buf + 24, 1);
    refuse("reserved.pack", buf, n, "reserved");

    n = build_pack(buf, HASH_A, HASH_B, NV2A_TEX_PACK_VERSION);
    wr32(buf + NV2A_TEX_PACK_HDR + 32 + 20, 1);      /* B's flags */
    refuse("flags.pack", buf, n, "flags");

    CHECK(nv2a_tex_pack_load("does-not-exist.pack", err, sizeof err) == -1
          && strstr(err, "open") != NULL, "missing file: '%s'", err);
    CHECK(nv2a_tex_pack_load("", err, sizeof err) == -1, "empty path loaded");
}

static void test_hash(void)
{
    uint8_t v[1034];
    int i;
    for (i = 0; i < 1024; i++)
        v[i] = (uint8_t)i;
    memcpy(v + 1024, "xboxrecomp", 10);
    CHECK(nv2a_tex_hash(v, sizeof v, NV2A_TEX_HASH_SEED) == 0x0B060D91FB09F186ull,
          "vector: %016llX", (unsigned long long)nv2a_tex_hash(v, sizeof v, NV2A_TEX_HASH_SEED));
    CHECK(nv2a_tex_hash(v, 0, NV2A_TEX_HASH_SEED) == 0x40500CA3D9E617F3ull, "empty");
    memset(v, 0, 64);
    CHECK(nv2a_tex_hash(v, 64, NV2A_TEX_HASH_SEED) == 0x69597731194007ABull, "zeros");
}

int main(void)
{
    test_write();
    test_refuse();
    test_hash();
    if (fails) {
        printf("tex_pack: %d failure(s)\n", fails);
        return 1;
    }
    printf("tex_pack: ok\n");
    return 0;
}
