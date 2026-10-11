/*
 * enhance.c - see enhance.h. Reads the toolkit's keys once and stores them
 * as nv2a_host_opts; the per-frame paths only ever read that struct.
 */
#include "enhance.h"

#include <stdio.h>
#include <string.h>

#include "enhance_cfg.h"
#include "kernel_pacing.h"
#include "nv2a_backend_common.h"
#include "nv2a_tex_pack.h"

#define LOGP "[ENHANCE] "
#define SCALE_MAX 4

static int path_is_absolute(const char *p)
{
    return p[0] == '/' || p[0] == '\\'
        || (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':');
}

/* textures.pack: a pack file, relative to the root the file itself is
 * read from (the executable's directory), so a bundle names
 * enhance/hud.pack. A bad or missing pack is said and the run stays
 * stock; nothing in the backends runs until nv2a_tex_pack_count() > 0. */
static void load_tex_pack(const char *root)
{
    const char *pack = enhance_cfg_string("textures.pack", "");
    char path[1024], err[128];
    int n;

    if (!pack || !*pack || !strcmp(pack, "none")) {
        nv2a_tex_pack_unload();     /* a re-init's "none" is true too */
        fprintf(stderr, LOGP "textures.pack=none\n");
        return;
    }
    if (root && *root && !path_is_absolute(pack))
        snprintf(path, sizeof path, "%s/%s", root, pack);
    else
        snprintf(path, sizeof path, "%s", pack);
    n = nv2a_tex_pack_load(path, err, sizeof err);
    if (n < 0)
        fprintf(stderr, LOGP "textures.pack=%s: %s (%s); no pack\n", pack, err, path);
    else
        fprintf(stderr, LOGP "textures.pack=%s (%d entries, scale %u)\n", pack, n,
                nv2a_tex_pack_scale());
}

static const char *const s_filters[] = { "nearest", "linear", "integer", NULL };
/* Indexes match XBOX_PACING_SPIN / XBOX_PACING_SLEEP. */
static const char *const s_pacing[] = { "spin", "sleep", NULL };

int xbox_enhance_init(const char *root, const char *title)
{
    struct nv2a_host_opts o;
    const char *aspect;
    long long scale;
    int rc, pacing;

    rc = enhance_cfg_init(root, title) < 0 ? -1 : 0;
    memset(&o, 0, sizeof o);

    scale = enhance_cfg_int("render.scale", 1);
    if (scale < 1 || scale > SCALE_MAX) {
        long long c = scale < 1 ? 1 : SCALE_MAX;
        fprintf(stderr, LOGP "render.scale=%lld out of range 1..%d; using %lld\n",
                scale, SCALE_MAX, c);
        scale = c;
    }
    o.render_scale = (unsigned)scale;
    /* Indexes match enum nv2a_present_filter. */
    o.present_filter = enhance_cfg_choice("present.filter", s_filters, NV2A_PRESENT_NEAREST);
    o.fullscreen = enhance_cfg_bool("present.fullscreen", 0);
    /* How lowered guest spin waits wait (kernel_pacing.h): only titles
     * generated with --spin-waits have any. Set before the guest starts. */
    pacing = enhance_cfg_choice("present.pacing", s_pacing, XBOX_PACING_SPIN);
    xbox_PacingSetMode(pacing);

    /* Hor+ is a later slice; a file or env that asks for it must not be
     * ignored silently. */
    aspect = enhance_cfg_string("display.aspect", "4:3");
    if (strcmp(aspect, "4:3") != 0) {
        fprintf(stderr, LOGP "display.aspect=%s not implemented yet (hor+ is a"
                " later slice); using 4:3\n", aspect);
        aspect = "4:3";
    }

    nv2a_host_opts_set(&o);
    fprintf(stderr, LOGP "render.scale=%u present.filter=%s present.fullscreen=%d"
            " present.pacing=%s (display.aspect=%s)\n", o.render_scale,
            s_filters[o.present_filter], o.fullscreen, s_pacing[pacing], aspect);
    load_tex_pack(root);
    /* No enhance_cfg_report_unused() here: the title reads its own keys
     * (game.mode, say) after this, and calls it then. */
    return rc;
}
