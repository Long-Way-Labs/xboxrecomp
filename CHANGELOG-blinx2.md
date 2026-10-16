# blinx2/portability changelog

This branch is apexJCL's fork of xboxrecomp that BLiNX 2 and Burnout 3 build
against: upstream plus POSIX hosts (macOS, Linux), a Metal backend and fixes
that have not reached upstream yet. Its releases are the `blinx2-vX.Y.Z` tags;
upstream's own versions are listed in the README's Changelog. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Entries start at
blinx2-v0.3.2; earlier tags are described by their commits.

## [Unreleased]

## [blinx2-v0.4.0] - 2026-10-15

### Added
- `render.aniso` (`RECOMP_RENDER_ANISO`): anisotropic filtering on
  mipmapped textures: 0 or 1 is stock (off), 2..16 the anisotropy; values
  outside 0..16 are clamped. Read at startup, on Direct3D 11 and Metal.
  The D3D11 side is by [@hgeorgilas](https://github.com/hgeorgilas).
- BUMPENVMAP and BUMPENVMAP_LUMINANCE texture stages on Direct3D 11 and
  Metal. The D3D11 side is by [@hgeorgilas](https://github.com/hgeorgilas).
  `render.aniso` leaves the stock filter on a texture stage that a
  BUMPENVMAP stage reads its offsets from; filtering it anisotropically
  turned BLiNX 2's far sea into noise. The CPU rasteriser still samples
  these modes as plain 2D.
- `RECOMP_TRACE=bump`: one `[BUMP]` line per distinct BUMPENVMAP stage
  setup (stage, input, matrix, scale, offset, formats) on both GPU
  backends.
- `RECOMP_GAME_FILES` (game files directory) and `RECOMP_HDD_DIR`
  (emulated hard disk) are now defined by the toolkit. A game that defined
  them itself must keep its rows under `#ifndef RECOMP_ENV_HAVE_FOLDER_ROWS`.
  The new-game template loads the enhancements layer, sets the window
  title and reads these keys.
- `RECOMP_DEBUG=occ_settle_us=<us>`: the per-frame bound on waiting for
  occlusion results (default 8000); 0 restores the old no-wait path.
- New ctests: `nv2a_bump`, `occ_reports`, `kernel_display_mode`,
  `kernel_path` and `fb_window_wanted`.

### Fixed
- The present size no longer alternates between the back buffers' clip
  sizes. Burnout 3's race-start card switched between 640x480 and 640x464
  every flip, so any scaled present jumped about 20 px. Recently presented
  buffers stay in the size scan, and the present uses the display mode's
  frame, so menus and races present at the same size.
- Visibility (occlusion) reports settle before the title reads them: at
  the end of each pushbuffer walk, before semaphore releases, and at the
  flip, waiting at most 8 ms a frame. Titles that skip draws on a stale
  report no longer drop them (BLiNX 2's characters on the Story Mode menu,
  Burnout 3's race-start flicker).
- `\Device\Harddisk0\Partition1\CACHE` (a title's `E:\CACHE`) resolves
  under the user-data root beside UDATA and TDATA (`RECOMP_SAVE_DIR`),
  not the game files directory, which a title could write into. Path rules
  match only at a path-component boundary.
- Windows: no GDI framebuffer window opens beside the Direct3D 11 backend.

## [blinx2-v0.3.2] - 2026-10-15

### Added
- The new-game template turns vblank and title kevents on, with a fence
  mirror example.
- Texture formats R8B8 (0x29) and LIN_R8B8 (0x16).
- The disassembler warns about call targets in code sections it did not
  sweep.
- The recompiler warns when a function body ends on `leave` / `pop ebp`
  with nothing after it (a likely cut-off function).
- New ctests: `host_nap`, `pb_semaphore` and `pgraph_fence`.

### Fixed
- Semaphore and report offsets are applied relative to the bound context
  DMA object, not as physical addresses, so a title whose fence object is
  not at address 0 no longer hangs in D3D's fence wait.
- With no fence mirror registered, PGRAPH 0x400B10 returns the executor's
  last semaphore release, so such titles no longer spin in D3D's fence
  wait. A registered mirror is never overridden.
- `RECOMP_VBLANK=0` now turns vblank delivery off (unset stays off).
- The disassembler and recompiler decode a code section up to its virtual
  end, not only its file bytes.
- Native Windows: the pushbuffer kick wait spins, then takes a
  high-resolution timer, instead of sleeping a whole timer tick, so busy
  scenes no longer drop frames (BLiNX 2's mission 1-2 city view fell to
  25 fps). `RECOMP_DEBUG=kick_nap=hybrid|timer|delay|spin` overrides the
  mode; Proton and POSIX hosts are unchanged.
