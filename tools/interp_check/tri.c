// tri <shots dir>: scores every in-between picture interp_<frame>_<i>of<k>.ppm (i < k) against the exact pictures of
// the previous game frame (its last picture) and of this one (<k>of<k>). Prints: frame i k motion novel score diff.
// motion: share of pixels where the two exact pictures differ; novel: share where the in-between picture differs
// from both (something neither frame has); score = novel / max(motion, 0.002); diff: mean change prev -> cur.
// Movement between two places keeps novel at about half of motion (score ~0.5); an object blended to a wrong place,
// stretched geometry or a picture from another scene scores high.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
typedef struct { int w, h; unsigned char* d; } Img;
static int load(const char* p, Img* im) {
    FILE* f = fopen(p, "rb"); if (!f) return 0;
    if (fscanf(f, "P6 %d %d 255", &im->w, &im->h) != 2) { fclose(f); return 0; }
    fgetc(f); size_t n = (size_t)im->w * im->h * 3; im->d = malloc(n);
    size_t r = fread(im->d, 1, n, f); fclose(f); return r == n;
}
typedef struct { int t, i, k; char name[64]; } E;
static int cmp(const void* a, const void* b) { const E *x = a, *y = b; return x->t != y->t ? x->t - y->t : x->i - y->i; }
int main(int argc, char** argv) {
    int T = 40; DIR* dir = opendir(argv[1]); struct dirent* de; E* es = malloc(sizeof(E) * 400000); int n = 0;
    while ((de = readdir(dir))) { E e; if (sscanf(de->d_name, "interp_%d_%dof%d.ppm", &e.t, &e.i, &e.k) == 3) { strcpy(e.name, de->d_name); es[n++] = e; } }
    qsort(es, n, sizeof(E), cmp);
    char p[4096];
    for (int j = 0; j < n; j++) {
        if (es[j].i >= es[j].k) continue;
        int pj = j - 1; while (pj >= 0 && es[pj].t == es[j].t) pj--; if (pj < 0) continue; // last picture of the previous tick
        int cj = j; while (cj < n && !(es[cj].t == es[j].t && es[cj].i == es[cj].k)) cj++; if (cj >= n) continue;
        Img a, m, b;
        snprintf(p, sizeof p, "%s/%s", argv[1], es[pj].name); if (!load(p, &a)) continue;
        snprintf(p, sizeof p, "%s/%s", argv[1], es[j].name); if (!load(p, &m)) continue;
        snprintf(p, sizeof p, "%s/%s", argv[1], es[cj].name); if (!load(p, &b)) continue;
        long motion = 0, novel = 0, px = (long)a.w * a.h; double md = 0;
        for (long q = 0; q < px * 3; q += 3) {
            int da = abs(m.d[q]-a.d[q]) + abs(m.d[q+1]-a.d[q+1]) + abs(m.d[q+2]-a.d[q+2]);
            int db = abs(m.d[q]-b.d[q]) + abs(m.d[q+1]-b.d[q+1]) + abs(m.d[q+2]-b.d[q+2]);
            int dab = abs(a.d[q]-b.d[q]) + abs(a.d[q+1]-b.d[q+1]) + abs(a.d[q+2]-b.d[q+2]);
            if (dab > T) motion++; if (da > T && db > T) novel++;
            md += dab;
        }
        double mo = (double)motion / px, no = (double)novel / px;
        printf("%d %d %d %.4f %.4f %.2f %.2f\n", es[j].t, es[j].i, es[j].k, mo, no, no / (mo > 0.002 ? mo : 0.002), md / px / 3);
        free(a.d); free(m.d); free(b.d);
    }
}
