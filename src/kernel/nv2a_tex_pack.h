/*
 * nv2a_tex_pack.h - a texture pack: host-side replacements for guest
 * textures, keyed by the hash the GPU backends already compute for their
 * per-flip change check (nv2a_tex_hash over the bytes nv2a_tex_extent
 * gives). The enhancements layer loads one (textures.pack); the Metal and
 * D3D11 uploads look their texel hash up here and, on a hit, create the
 * host texture at the pack entry's size from the pack's texels. The guest
 * texture, the dump and the game are untouched: a pack is a host-only
 * view, and a texture the game rewrites stops matching.
 *
 * File format (docs/formats/texture-pack.md), little-endian:
 *   header  "XRTP", u32 version (1), u32 count, u32 scale,
 *           u64 index offset, u64 reserved
 *   index   count x 32 bytes, sorted by hash: u64 hash, u32 width,
 *           u32 height, u32 levels, u32 flags, u64 payload offset
 *   payload per entry, level 0 then each further level, one A8R8G8B8
 *           texel per pixel (the bytes nv2a_tex_decode produces)
 *
 * The index is read at load; an entry's texels are read on first use and
 * kept. Lives in the kernel library like nv2a_host_opts, so the backends
 * never link the enhancements layer: without a pack nv2a_tex_pack_count()
 * is 0 and nothing else runs. Not thread-safe: load at startup, use from
 * the thread that uploads textures.
 */
#ifndef NV2A_TEX_PACK_H
#define NV2A_TEX_PACK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NV2A_TEX_PACK_MAGIC   "XRTP"
#define NV2A_TEX_PACK_VERSION 1u
#define NV2A_TEX_PACK_HDR     32u
#define NV2A_TEX_PACK_ENTRY   32u
#define NV2A_TEX_PACK_MAX_DIM 16384u   /* D3D11 and Metal texture limit */
#define NV2A_TEX_PACK_MAX_ENTRIES (1u << 20)

struct nv2a_tex_pack_entry {
    uint64_t  hash;
    uint32_t  width, height, levels, flags;
    uint64_t  offset;         /* of the texels in the file */
    size_t    texels;         /* over every level */
    uint32_t *data;           /* read on first use; NULL until then */
};

/* Load `path`: validates the header, the index bounds against the file
 * size and the sort order, and keeps the index. Returns the entry count,
 * or -1 with the reason in `err` (the file is then not loaded). A second
 * load replaces the first. */
int nv2a_tex_pack_load(const char *path, char *err, size_t errlen);
void nv2a_tex_pack_unload(void);

unsigned nv2a_tex_pack_count(void);
unsigned nv2a_tex_pack_scale(void);
/* Hits of nv2a_tex_pack_find, for the backends' statistics. */
uint64_t nv2a_tex_pack_hits(void);

/* The entry for a texel hash, or NULL. The hash is the one the backend
 * computed for its cache entry; a P8 texture's folds its palette in and
 * so never matches, which is right (the pack holds no palettes). An entry
 * whose aspect differs from the guest texture's (guest_w x guest_h) is
 * not taken, nor counted as a hit. */
struct nv2a_tex_pack_entry *nv2a_tex_pack_find(uint64_t hash, uint32_t guest_w,
                                               uint32_t guest_h);

/* The entry's texels (e->texels of them, level 0 first), read from the
 * file on the first call; NULL if the read failed (the caller then
 * uploads the guest texels as if there were no entry). */
const uint32_t *nv2a_tex_pack_texels(struct nv2a_tex_pack_entry *e);

#ifdef __cplusplus
}
#endif

#endif /* NV2A_TEX_PACK_H */
