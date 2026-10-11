# Texture pack (`XRTP`)

A texture pack holds host-side replacements for guest textures, keyed by
the hash the GPU backends compute for every texture they upload. The
enhancements layer loads one through `textures.pack`
([enhance-config.md](../runtime/enhance-config.md)); the Metal and D3D11
backends look each upload's texel hash up in it and, on a hit, create the
host texture at the pack entry's size from the pack's texels instead of
the decoded guest texels. The guest texture, the game's files and the CPU
rasteriser are untouched.

A pack is made from a game's own files, so it belongs to the owner of
those files: it is generated on their machine (`xbr enhance hud` in
xboxrecomp-cli) and is not something to distribute.

## Key

The key of an entry is `nv2a_tex_hash` (`src/kernel/nv2a_backend_common.c`:
a four-lane FNV-1a over 32-byte groups, the tail byte by byte, the lanes
folded at the end; seed `NV2A_TEX_HASH_SEED`) over the bytes
`nv2a_tex_extent` gives for the guest texture: for a swizzled or DXT
texture, every level packed (for DXT, the block bytes); for a linear one,
`pitch * (h - 1) + w * bytes_per_texel`. For a DDS file that the game loads
with D3DX as it is, that is the file's data block, which is how a pack can
be built offline from the game's files.

A P8 texture's cache key folds its palette into the hash, so P8 textures
never match a pack entry.

Test vector, shared with the CLI's Python port: the 1034 bytes `0..255`
four times followed by `xboxrecomp` hash to `0x0B060D91FB09F186`; the
empty input to `0x40500CA3D9E617F3`; 64 zero bytes to `0x69597731194007AB`.

## Layout

All fields little-endian.

```
offset  size  field
0       4     magic "XRTP"
4       4     version, 1
8       4     count
12      4     scale: the factor the entries were made at (informational;
              logged as "scale N")
16      8     index offset (>= 32)
24      8     reserved, 0 (a v1 reader refuses any other value)

index: count entries of 32 bytes, sorted by hash, strictly increasing
0       8     hash (nv2a_tex_hash of the guest texture)
8       4     width  (1..16384)
12      4     height (1..16384)
16      4     levels (1 .. 1 + floor(log2(max(w, h))))
20      4     flags, 0 (a v1 reader refuses any other value)
24      8     payload offset (>= 32)

payload: per entry, levels * (w x h) texels, level 0 first, each further
level halving width and height (floor, minimum 1); one u32 A8R8G8B8 texel
per pixel: the value nv2a_tex_decode produces, which the backends upload
as BGRA8 bytes
```

The reader (`src/kernel/nv2a_tex_pack.c`) checks the magic and version,
that the index and every entry's texels lie inside the file, the sizes,
and the sort order, and refuses the whole file otherwise; the layer logs
the reason and the run stays stock. The index is read at load; an entry's
texels are read on first use and kept for the process. The sampler state
comes from the guest's registers as always, so a replacement is sampled
with the guest's filter and wrap modes and, since swizzled and DXT
textures use normalised coordinates, needs no other change. A linear
texture (texel coordinates) would be sampled at guest texel positions and
should not be put in a pack.

A sidecar `hud-pack.toml` written by the CLI beside the pack describes
the entries for people (archive entry names, hashes, sizes, the scale and
kernel, the game's XBE sha256); the runtime never reads it.

Test: `tests/tex_pack` (ctest `tex_pack`).
