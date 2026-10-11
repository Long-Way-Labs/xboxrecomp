/*
 * nv2a_tex_pack.c - see nv2a_tex_pack.h.
 */
#include "nv2a_tex_pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define pack_seek(f, off) _fseeki64((f), (long long)(off), SEEK_SET)
#define pack_tell(f)      _ftelli64(f)
#else
#define pack_seek(f, off) fseeko((f), (off_t)(off), SEEK_SET)
#define pack_tell(f)      ftello(f)
#endif

static struct nv2a_tex_pack_entry *s_entries;
static unsigned  s_count, s_scale;
static char     *s_path;
static uint64_t  s_hits;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

/* Texels over `levels` levels of w x h, each level halving (floor, min 1):
 * the layout nv2a_tex_texels gives, written out here so the reader has no
 * dependency on the backend-common code. */
static size_t level_texels(uint32_t w, uint32_t h, uint32_t levels)
{
    size_t n = 0;
    uint32_t l;
    for (l = 0; l < levels; l++) {
        n += (size_t)w * h;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    return n;
}

/* Levels a w x h chain can have: 1 + floor(log2(max(w, h))). More would
 * repeat 1x1 levels, which D3D11 refuses (the texture draws black) and
 * Metal reports as invalid. */
static uint32_t max_levels(uint32_t w, uint32_t h)
{
    uint32_t m = w > h ? w : h, n = 1;
    while (m >>= 1)
        n++;
    return n;
}

static void fail(char *err, size_t errlen, const char *msg)
{
    if (err && errlen)
        snprintf(err, errlen, "%s", msg);
}

void nv2a_tex_pack_unload(void)
{
    unsigned i;
    for (i = 0; i < s_count; i++)
        free(s_entries[i].data);
    free(s_entries);
    free(s_path);
    s_entries = NULL;
    s_path = NULL;
    s_count = 0;
    s_scale = 0;
    s_hits = 0;
}

int nv2a_tex_pack_load(const char *path, char *err, size_t errlen)
{
    uint8_t hdr[NV2A_TEX_PACK_HDR];
    uint8_t *index = NULL;
    FILE *f;
    long long size;
    uint64_t index_off;
    uint32_t count, version, scale;
    unsigned i;
    struct nv2a_tex_pack_entry *entries = NULL;
    const char *why = NULL;

    nv2a_tex_pack_unload();
    if (!path || !*path) {
        fail(err, errlen, "no path");
        return -1;
    }
    f = fopen(path, "rb");
    if (!f) {
        fail(err, errlen, "cannot open");
        return -1;
    }
    if (pack_seek(f, 0) != 0 || fseek(f, 0, SEEK_END) != 0 || (size = pack_tell(f)) < 0) {
        fclose(f);
        fail(err, errlen, "cannot read");
        return -1;
    }
    if ((uint64_t)size < NV2A_TEX_PACK_HDR || pack_seek(f, 0) != 0
            || fread(hdr, 1, sizeof hdr, f) != sizeof hdr) {
        fclose(f);
        fail(err, errlen, "too short for a pack header");
        return -1;
    }
    if (memcmp(hdr, NV2A_TEX_PACK_MAGIC, 4) != 0) {
        fclose(f);
        fail(err, errlen, "not a texture pack (bad magic)");
        return -1;
    }
    version = rd32(hdr + 4);
    count = rd32(hdr + 8);
    scale = rd32(hdr + 12);
    index_off = rd64(hdr + 16);
    if (version != NV2A_TEX_PACK_VERSION)
        why = "unsupported pack version";
    /* Zero in v1, so a later reader can give them a meaning a v1 reader
     * refuses instead of misreading. */
    else if (rd64(hdr + 24) != 0)
        why = "non-zero reserved header field";
    else if (count > NV2A_TEX_PACK_MAX_ENTRIES)
        why = "too many entries";
    /* Subtracted, not added: an offset near 2^64 must not wrap past the check. */
    else if (index_off < NV2A_TEX_PACK_HDR || index_off > (uint64_t)size
             || (uint64_t)count * NV2A_TEX_PACK_ENTRY > (uint64_t)size - index_off)
        why = "index outside the file";
    if (why) {
        fclose(f);
        fail(err, errlen, why);
        return -1;
    }
    if (count) {
        index = (uint8_t *)malloc((size_t)count * NV2A_TEX_PACK_ENTRY);
        entries = (struct nv2a_tex_pack_entry *)calloc(count, sizeof *entries);
        if (!index || !entries || pack_seek(f, index_off) != 0
                || fread(index, NV2A_TEX_PACK_ENTRY, count, f) != count) {
            free(index);
            free(entries);
            fclose(f);
            fail(err, errlen, "cannot read the index");
            return -1;
        }
    }
    fclose(f);
    for (i = 0; i < count && !why; i++) {
        const uint8_t *p = index + (size_t)i * NV2A_TEX_PACK_ENTRY;
        struct nv2a_tex_pack_entry *e = &entries[i];
        e->hash = rd64(p);
        e->width = rd32(p + 8);
        e->height = rd32(p + 12);
        e->levels = rd32(p + 16);
        e->flags = rd32(p + 20);
        e->offset = rd64(p + 24);
        if (!e->width || !e->height || e->width > NV2A_TEX_PACK_MAX_DIM
                || e->height > NV2A_TEX_PACK_MAX_DIM || !e->levels
                || e->levels > max_levels(e->width, e->height))
            why = "entry with a bad size";
        else if (e->flags)
            why = "entry with non-zero flags";
        else if (i && entries[i - 1].hash >= e->hash)
            why = "index not sorted by hash";
        else {
            e->texels = level_texels(e->width, e->height, e->levels);
            if (e->offset < NV2A_TEX_PACK_HDR || e->offset > (uint64_t)size
                    || (uint64_t)e->texels * 4u > (uint64_t)size - e->offset)
                why = "entry texels outside the file";
        }
    }
    free(index);
    if (why) {
        free(entries);
        fail(err, errlen, why);
        return -1;
    }
    s_path = (char *)malloc(strlen(path) + 1);
    if (!s_path) {
        free(entries);
        fail(err, errlen, "out of memory");
        return -1;
    }
    strcpy(s_path, path);
    s_entries = entries;
    s_count = count;
    s_scale = scale;
    return (int)count;
}

unsigned nv2a_tex_pack_count(void)
{
    return s_count;
}

unsigned nv2a_tex_pack_scale(void)
{
    return s_scale;
}

uint64_t nv2a_tex_pack_hits(void)
{
    return s_hits;
}

struct nv2a_tex_pack_entry *nv2a_tex_pack_find(uint64_t hash, uint32_t guest_w,
                                               uint32_t guest_h)
{
    unsigned lo = 0, hi = s_count;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        if (s_entries[mid].hash < hash)
            lo = mid + 1;
        else if (s_entries[mid].hash > hash)
            hi = mid;
        else {
            struct nv2a_tex_pack_entry *e = &s_entries[mid];
            /* The hash covers the bytes, not the shape: a 64x16 and a 32x32
             * texture of the same bytes (a uniform fill) hash alike, so an
             * entry of another aspect is some other texture. */
            if ((uint64_t)e->width * guest_h != (uint64_t)e->height * guest_w)
                return NULL;
            s_hits++;
            return e;
        }
    }
    return NULL;
}

const uint32_t *nv2a_tex_pack_texels(struct nv2a_tex_pack_entry *e)
{
    FILE *f;
    uint32_t *data;
    if (!e)
        return NULL;
    if (e->data)
        return e->data;
    data = (uint32_t *)malloc(e->texels * 4u);
    if (!data)
        return NULL;
    f = s_path ? fopen(s_path, "rb") : NULL;
    if (!f || pack_seek(f, e->offset) != 0 || fread(data, 4u, e->texels, f) != e->texels) {
        /* Said for the first four failures only: the guest texels are
         * uploaded instead, and the next upload tries again. */
        static unsigned said;
        if (said++ < 4)
            fprintf(stderr, "[ENHANCE] textures.pack: cannot read entry %016llX from %s\n",
                    (unsigned long long)e->hash, s_path ? s_path : "?");
        if (f)
            fclose(f);
        free(data);
        return NULL;
    }
    fclose(f);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    {
        size_t i;
        for (i = 0; i < e->texels; i++) {
            uint32_t v = data[i];
            data[i] = (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24);
        }
    }
#endif
    e->data = data;
    return data;
}
