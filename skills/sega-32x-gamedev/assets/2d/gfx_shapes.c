/* gfx_shapes.c - drop-in 2D scanline shape fills for the packed 8bpp 32X back
 * buffer. Add the prototypes to gfx.h and these bodies to gfx.c. They write the
 * same buffer as GFX_FillRect: Mars_BackBufferPixels() with stride SCREEN_W.
 *
 * void GFX_FillTri(int x0,int y0,int x1,int y1,int x2,int y2,u8 c);
 * void GFX_FillCircle(int cx,int cy,int r,u8 c);
 * void GFX_FillPoly(int cx,int cy,const signed char *pts,int n,int num,int den,u8 c);
 *
 * FillPoly triangle-fans from (cx,cy) over relative point pairs pts[2*n] scaled
 * by num/den. Good for centre-visible ship/enemy shapes.
 */
#include "gfx.h"
#include "mars.h"

static void gfx_hspan(int x0, int x1, int y, u8 c)
{
    u8 *d; int x;
    if (y < 0 || y >= SCREEN_H) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (x0 < 0) x0 = 0;
    if (x1 >= SCREEN_W) x1 = SCREEN_W - 1;
    if (x0 > x1) return;
    d = (u8 *)Mars_BackBufferPixels() + y * SCREEN_W + x0;
    for (x = x0; x <= x1; x++) *d++ = c;
}

void GFX_FillTri(int x0, int y0, int x1, int y1, int x2, int y2, u8 c)
{
    int ymin = y0, ymax = y0, y;
    if (y1 < ymin) ymin = y1; if (y2 < ymin) ymin = y2;
    if (y1 > ymax) ymax = y1; if (y2 > ymax) ymax = y2;
    if (ymin < 0) ymin = 0; if (ymax >= SCREEN_H) ymax = SCREEN_H - 1;
    for (y = ymin; y <= ymax; y++) {
        int xl = 0x7fffffff, xr = -0x7fffffff, e;
        #define EDGE(ax,ay,bx,by) do { int _a=ay,_b=by; \
            if ((y>=_a&&y<=_b)||(y>=_b&&y<=_a)) { \
                if (_a==_b) { if(ax<xl)xl=ax; if(bx<xl)xl=bx; if(ax>xr)xr=ax; if(bx>xr)xr=bx; } \
                else { e=(ax)+((bx)-(ax))*(y-_a)/(_b-_a); if(e<xl)xl=e; if(e>xr)xr=e; } } } while(0)
        EDGE(x0,y0,x1,y1); EDGE(x1,y1,x2,y2); EDGE(x2,y2,x0,y0);
        #undef EDGE
        if (xr >= xl) gfx_hspan(xl, xr, y, c);
    }
}

void GFX_FillCircle(int cx, int cy, int r, u8 c)
{
    int dy;
    if (r < 1) r = 1;
    for (dy = -r; dy <= r; dy++) {
        int rem = r*r - dy*dy, dx = 0;
        while ((dx+1)*(dx+1) <= rem) dx++;          /* integer sqrt */
        gfx_hspan(cx - dx, cx + dx, cy + dy, c);
    }
}

void GFX_FillPoly(int cx, int cy, const signed char *pts, int n, int num, int den, u8 c)
{
    int i;
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;
        int ax = cx + pts[2*i]*num/den,   ay = cy + pts[2*i+1]*num/den;
        int bx = cx + pts[2*j]*num/den,   by = cy + pts[2*j+1]*num/den;
        GFX_FillTri(cx, cy, ax, ay, bx, by, c);
    }
}
