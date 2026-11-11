#include "app.h"
#include "icons.h"
#include <stdlib.h>
#include <string.h>
/* Rasterize only our embedded, trusted Lucide SVG files. The original SVGs
   stay intact; white masks let CUI apply the current theme's color. */
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#define NANOSVG_IMPLEMENTATION
#include "../third_party/nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "../third_party/nanosvg/nanosvgrast.h"
#ifdef _MSC_VER
#pragma warning(pop)
#pragma warning(disable: 4702)
#endif
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
bool icon_pixels(int index, int size, unsigned char *rgba) {
    HRSRC res = FindResourceW(app.instance, MAKEINTRESOURCEW(200 + index), RT_RCDATA);
    if (!res)
        return false;
    DWORD length = SizeofResource(app.instance, res);
    const void *data = LockResource(LoadResource(app.instance, res));
    char *svg = malloc((size_t)length + 1);
    if (!data || !svg) {
        free(svg);
        return false;
    }
    memcpy(svg, data, length);
    svg[length] = 0;
    NSVGimage *image = nsvgParse(svg, "px", 96);
    free(svg);
    if (!image)
        return false;
    for (NSVGshape *shape = image->shapes; shape; shape = shape->next) {
        if (shape->stroke.type == NSVG_PAINT_COLOR)
            shape->stroke.color = 0xffffffff;
        if (shape->fill.type == NSVG_PAINT_COLOR)
            shape->fill.color = 0xffffffff;
    }
    NSVGrasterizer *raster = nsvgCreateRasterizer();
    if (raster) {
        nsvgRasterize(raster, image, 0, 0, (float)size / 24.f, rgba, size, size, size * 4);
        nsvgDeleteRasterizer(raster);
    }
    nsvgDelete(image);
    return raster != NULL;
}

/* Compose the notification dot over the original application logo at runtime.
   The original ICO remains unchanged, including its size-specific artwork. */
HICON icon_update_badge(HICON original) {
    ICONINFO info = {0};
    if (!GetIconInfo(original, &info)) return NULL;
    BITMAP bm = {0};
    GetObjectW(info.hbmColor, sizeof(bm), &bm);
    int size = bm.bmWidth;
    HICON result = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    BITMAPINFO dib = {.bmiHeader = {.biSize = sizeof(BITMAPINFOHEADER), .biWidth = size,
        .biHeight = -size, .biPlanes = 1, .biBitCount = 32, .biCompression = BI_RGB}};
    unsigned char *pixels = NULL;
    HBITMAP color = size > 0 && dc ? CreateDIBSection(dc, &dib, DIB_RGB_COLORS, (void **)&pixels, NULL, 0) : NULL;
    unsigned char *badge = size > 0 ? calloc((size_t)size * size, 4) : NULL;
    char svg[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\">"
                 "<circle cx=\"20\" cy=\"20\" r=\"3.5\" fill=\"#f09a38\" stroke=\"#ffffff\" stroke-width=\"1.2\"/></svg>";
    NSVGimage *image = nsvgParse(svg, "px", 96);
    NSVGrasterizer *raster = nsvgCreateRasterizer();
    if (color && badge && image && raster) {
        if (GetDIBits(dc, info.hbmColor, 0, (UINT)size, pixels, &dib, DIB_RGB_COLORS) == size) {
            nsvgRasterize(raster, image, 0, 0, (float)size / 24.f, badge, size, size, size * 4);
            for (int i = 0; i < size * size; i++) {
                unsigned char *dst = pixels + i * 4, *src = badge + i * 4;
                unsigned a = src[3], inverse = 255 - a;
                if (!a) continue;
                unsigned alpha = a * 255 + dst[3] * inverse;
                for (int c = 0; c < 3; c++)
                    dst[c] = (unsigned char)((src[2 - c] * a * 255 + dst[c] * dst[3] * inverse + alpha / 2) / alpha);
                dst[3] = (unsigned char)((alpha + 127) / 255);
            }
            ICONINFO composed = info; composed.hbmColor = color;
            result = CreateIconIndirect(&composed);
        }
    }
    if (raster) nsvgDeleteRasterizer(raster);
    if (image) nsvgDelete(image);
    free(badge);
    if (color) DeleteObject(color);
    if (dc) DeleteDC(dc);
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    return result;
}
