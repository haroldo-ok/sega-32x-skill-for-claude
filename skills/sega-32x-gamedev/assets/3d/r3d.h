/* r3d - tiny fixed-point software 3D for the 32X: transform, perspective
 * project, and flat-shaded triangle rasterization into an 8bpp buffer.
 * HAL-free (operates on a caller-supplied framebuffer) so it host-tests. */
#ifndef R3D_H
#define R3D_H

typedef int fx;                 /* 16.16 fixed point */
#define FX_ONE 65536
#define FX(n)  ((fx)((n) * 65536))
static inline fx fmul(fx a, fx b){ return (fx)(((long long)a * b) >> 16); }

typedef struct { fx x, y, z; } vec3;
typedef struct { unsigned char *px; int w, h; } fb_t;

typedef struct { int a, b, c; unsigned char color; } tri_t;
typedef struct {
    const vec3 *verts; int nverts;
    const tri_t *tris;  int ntris;
} mesh_t;

typedef struct {
    vec3 pos;           /* camera world position */
    int  yaw;           /* 0..255 = full turn */
    int  focal;         /* projection focal length in pixels */
} cam_t;

int  r3d_sin(int a);            /* 16.16 */
int  r3d_cos(int a);
void r3d_fill_tri(fb_t *fb, int x0,int y0,int x1,int y1,int x2,int y2, unsigned char col);
/* project a world point; returns 1 and sets sx,sy if in front of camera */
int  r3d_project(const cam_t *c, const fb_t *fb, vec3 p, int *sx, int *sy);
/* transform+project+painter-sort+rasterize a mesh at world offset `at` with yaw `ryaw` */
void r3d_draw_mesh(fb_t *fb, const cam_t *c, const mesh_t *m, vec3 at, int ryaw);

#endif
