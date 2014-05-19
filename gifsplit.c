#define _POSIX_C_SOURCE 2

#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <assert.h>

#include <png.h>
#include "libgifsplit.h"

int verbose = 0;

static void usage(const char *argv0)
{
    fprintf(stderr, "Usage: %s [-v] input.gif output_base\n", argv0);
}

static void dbgprintf(const char *fmt, ...) {
    va_list ap;

    if (!verbose)
        return;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static bool write_image(GifSplitImage *img, const char *filename,
                        png_bytepp row_pointers)
{
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        return false;
    }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                                  NULL, NULL, NULL);
    if (!png_ptr)
        return false;

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, NULL);
        return false;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        fprintf(stderr, "libpng returned an error\n");
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return false;
    }
    png_init_io(png_ptr, fp);

    size_t stride;
    if (img->IsTruecolor) {
        png_set_IHDR(png_ptr, info_ptr, img->Width, img->Height,
                     8, PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE,
                     PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
        stride = 4 * img->Width;
    } else {
        assert(img->ColorMap);
        int bpp = img->ColorMap->BitsPerPixel;
        /* Round to next power of two */
        while (bpp & (bpp - 1))
            bpp++;
        png_set_IHDR(png_ptr, info_ptr, img->Width, img->Height,
                     bpp, PNG_COLOR_TYPE_PALETTE,
                     PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                     PNG_FILTER_TYPE_DEFAULT);
        /* GifColorType should have the same layout as png_color */
        png_set_PLTE(png_ptr, info_ptr, (png_color*)img->ColorMap->Colors,
                     img->ColorMap->ColorCount);
        if (img->TransparentColorIndex != -1) {
            png_byte trans_alpha[256];
            memset(trans_alpha, 255, img->TransparentColorIndex);
            trans_alpha[img->TransparentColorIndex] = 0;
            png_set_tRNS(png_ptr, info_ptr, trans_alpha,
                         img->TransparentColorIndex + 1, NULL);
        }
        stride = img->Width;
    }

    png_bytep p = img->RasterData;
    for (int i = 0; i < img->Height; i++) {
        row_pointers[i] = p;
        p += stride;
    }
    png_set_rows(png_ptr, info_ptr, row_pointers);
    png_write_png(png_ptr, info_ptr,
                  img->IsTruecolor ? 0 : PNG_TRANSFORM_PACKING, NULL);

    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
    return true;
}

int main(int argc, char **argv)
{
    int opt;
    while ((opt = getopt(argc, argv, "v")) != -1) {
        switch (opt) {
        case 'v':
            verbose = 1;
            break;
        default: /* 'h' */
            usage(argv[0]);
            return 1;
        }
    }

    if (optind != (argc - 2)) {
        fprintf(stderr, "Expected 2 arguments after options\n");
        return 1;
    }

    const char *in_filename = argv[optind];
    const char *output_base = argv[optind + 1];
    size_t fn_len = strlen(output_base) + 64;
    char *output_filename = malloc(fn_len + 1);
    if (!output_filename) {
        fprintf(stderr, "Out of memory\n");
        return 1;
    }
    memset(output_filename, 0, fn_len + 1);

    dbgprintf("Opening %s...\n", in_filename);

    GifFileType *gif;

    if (!strcmp(in_filename, "-"))
        gif = DGifOpenFileHandle(0);
    else
        gif = DGifOpenFileName(in_filename);

    if (!gif) {
        fprintf(stderr, "Failed to open %s\n", in_filename);
        return 1;
    }

    GifSplitHandle *handle = GifSplitterOpen(gif);
    if (!handle) {
        fprintf(stderr, "Failed to greate GIF splitter handle\n");
        return 1;
    }

    GifSplitImage *img;
    int frame = 0;

    png_bytepp row_pointers;
    row_pointers = malloc(sizeof(*row_pointers) * gif->SHeight);
    if (!row_pointers) {
        fprintf(stderr, "Out of memory\n");
        return 1;
    }

    while ((img = GifSplitterReadFrame(handle))) {
        dbgprintf("Read frame %d (truecolor=%d, cmap=%d)\n", frame,
                  img->IsTruecolor, img->UsedLocalColormap);
        snprintf(output_filename, fn_len, "%s%06d.png", output_base, frame);
        if (!write_image(img, output_filename, row_pointers)) {
            fprintf(stderr, "Failed to write to %s\n", output_filename);
            return 1;
        }
        printf("%d delay=%d\n", frame, img->DelayTime);
        frame++;
    }

    GifSplitInfo *info;
    info = GifSplitterGetInfo(handle);
    if (info->HasErrors) {
        fprintf(stderr, "Error while processing input gif\n");
        return 1;
    }
    if (info)
        printf("loops=%d\n", info->LoopCount);

    GifSplitterClose(handle);
    free(row_pointers);
    free(output_filename);
    return 0;
}
