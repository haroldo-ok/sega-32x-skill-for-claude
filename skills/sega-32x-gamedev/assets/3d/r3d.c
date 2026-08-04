/* r3d.c - fixed-point software 3D: see r3d.h. Clean-room implementation. */
#include "r3d.h"
#include "sintab.inc"
#include "recip.inc"

int r3d_sin(int a){ return sintab[a & 255]; }
int r3d_cos(int a){ return sintab[(a + 64) & 255]; }

/* Flat-shaded triangle fill (scanline). Writes color index into fb->px. */
void r3d_fill_tri(fb_t *fb, int x0,int y0,int x1,int y1,int x2,int y2,
                  unsigned char col)
{
    int tx, ty;
    /* sort by y: (x0,y0) top .. (x2,y2) bottom */
    if (y0 > y1){ tx=x0;x0=x1;x1=tx; ty=y0;y0=y1;y1=ty; }
    if (y0 > y2){ tx=x0;x0=x2;x2=tx; ty=y0;y0=y2;y2=ty; }
    if (y1 > y2){ tx=x1;x1=x2;x2=tx; ty=y1;y1=y2;y2=ty; }
    if (y2 == y0) return;                         /* degenerate */

    for (int y = (y0 < 0 ? 0 : y0); y <= y2 && y < fb->h; y++) {
        int xa, xb;
        /* long edge x0->x2 */
        xa = x0 + (int)((long long)(x2 - x0) * (y - y0) / (y2 - y0));
        /* short edge: top half x0->x1, bottom half x1->x2 */
        if (y < y1 && y1 != y0)
            xb = x0 + (int)((long long)(x1 - x0) * (y - y0) / (y1 - y0));
        else if (y2 != y1)
            xb = x1 + (int)((long long)(x2 - x1) * (y - y1) / (y2 - y1));
        else
            xb = x1;
        if (xa > xb){ int t=xa; xa=xb; xb=t; }
        if (xa < 0) xa = 0;
        if (xb >= fb->w) xb = fb->w - 1;
        if (xa <= xb) {
            unsigned char *row = fb->px + y * fb->w + xa;
            for (int x = xa; x <= xb; x++) *row++ = col;
        }
    }
}

/* world -> camera space (translate by -pos, rotate by -yaw around Y) */
static vec3 to_camera(const cam_t *c, vec3 p)
{
    fx dx = p.x - c->pos.x, dy = p.y - c->pos.y, dz = p.z - c->pos.z;
    fx s = r3d_sin(c->yaw), co = r3d_cos(c->yaw);
    vec3 o;
    o.x = fmul(dx, co) - fmul(dz, s);
    o.z = fmul(dx, s)  + fmul(dz, co);
    o.y = dy;
    return o;
}

/* fast 1/z: z is 16.16 (>0). key = z>>12; recip_tab[key] ~= (1<<RECIP_SH)/key.
 * Since key = z/4096 = z_fixed/2^12, (1<<RECIP_SH)/key = (1<<(RECIP_SH+12))/z.
 * Multiplying coord*focal by this and shifting by (RECIP_SH+12) gives pixels. */
static int project_cam(const fb_t *fb, const cam_t *c, vec3 cs, int *sx, int *sy)
{
    unsigned key; unsigned long long r;
    if (cs.z < FX(1)/4) return 0;                 /* behind / too near */
    key = (unsigned)cs.z >> 12;
    if (key >= RECIP_N) key = RECIP_N - 1;
    r = recip_tab[key];                            /* (1<<RECIP_SH)/key */
    *sx = fb->w/2 + (int)(((long long)cs.x * c->focal * (long long)r) >> (RECIP_SH + 12));
    *sy = fb->h/2 - (int)(((long long)cs.y * c->focal * (long long)r) >> (RECIP_SH + 12));
    return 1;
}

int r3d_project(const cam_t *c, const fb_t *fb, vec3 p, int *sx, int *sy)
{
    return project_cam(fb, c, to_camera(c, p), sx, sy);
}

/* transform+project+painter-sort+rasterize a mesh at world offset `at`,
 * pre-rotated about its own Y by ryaw. Painter's algorithm (no z-buffer). */
void r3d_draw_mesh(fb_t *fb, const cam_t *c, const mesh_t *m, vec3 at, int ryaw)
{
    static int  sxb[256], syb[256], okb[256];
    static long facez[512]; static int order[512];
    fx s = r3d_sin(ryaw), co = r3d_cos(ryaw);
    int i, j;

    for (i = 0; i < m->nverts && i < 256; i++) {
        vec3 v = m->verts[i], w, cs;
        w.x = fmul(v.x, co) - fmul(v.z, s) + at.x;   /* model yaw + place */
        w.z = fmul(v.x, s)  + fmul(v.z, co) + at.z;
        w.y = v.y + at.y;
        cs = to_camera(c, w);
        okb[i] = project_cam(fb, c, cs, &sxb[i], &syb[i]);
        /* stash camera-z per vertex for face sorting */
        facez[i] = cs.z;
    }

    for (i = 0; i < m->ntris && i < 512; i++) order[i] = i;
    /* sort faces back-to-front by average vertex camera-z (simple insertion) */
    for (i = 0; i < m->ntris; i++) {
        const tri_t *t = &m->tris[i];
        facez[i] = ((long)facez[t->a] + facez[t->b] + facez[t->c]);
    }
    for (i = 1; i < m->ntris; i++) {
        int k = order[i]; long kz = facez[k];
        for (j = i - 1; j >= 0 && facez[order[j]] < kz; j--) order[j+1] = order[j];
        order[j+1] = k;
    }
    for (i = 0; i < m->ntris; i++) {
        const tri_t *t = &m->tris[order[i]];
        if (!okb[t->a] || !okb[t->b] || !okb[t->c]) continue;
        r3d_fill_tri(fb, sxb[t->a],syb[t->a], sxb[t->b],syb[t->b],
                     sxb[t->c],syb[t->c], t->color);
    }
}
