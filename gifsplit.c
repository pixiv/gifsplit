#define _POSIX_C_SOURCE 2

#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <assert.h>

#include <png.h>
#include <jpeglib.h>
#include "libgifsplit.h"

#define ERR_UNSPECIFIED 1
#define ERR_MAX_FRAMES  2

int verbose = 0;
bool jpeg = false;
bool optimize = false;
int quality = 0;
int sampling = -1;
int max_frames = 0;

static void usage(const char *argv0)
{
    fprintf(stderr, "Usage: %s [OPTIONS] input.gif output_base\n", argv0);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -h             show this help\n");
    fprintf(stderr, "  -V             display version number and exit\n");
    fprintf(stderr, "  -v             verbose debugging output\n");
    fprintf(stderr, "  -q QUALITY     output JPEGs instead of PNGs\n");
    fprintf(stderr, "                 (specify the quality level, 0-100)\n");
    fprintf(stderr, "  -s [012]       set color subsampling:\n");
    fprintf(stderr, "                   0: 4:4:4 (no subsampling)\n");
    fprintf(stderr, "                   1: 4:2:2 (2x1 subsampling)\n");
    fprintf(stderr, "                   2: 4:2:0 (2x2 subsampling)\n");
    fprintf(stderr, "                 default: 2 for q<90, else 0\n");
    fprintf(stderr, "  -o             optimize the JPEG Huffman tables\n");
    fprintf(stderr, "  -m [NUMBER]    limit number of frames to output\n");
}

static void dbgprintf(const char *fmt, ...) {
    va_list ap;

    if (!verbose)
        return;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static bool write_jpeg(GifSplitImage *img, const char *filename)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    int row_stride = img->Width * 3;

    JSAMPROW row_pointer[1];
    JSAMPLE *row = malloc(row_stride);
    if (!row) {
        fprintf(stderr, "Out of memory\n");
        return false;
    }
    row_pointer[0] = row;

    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        return false;
    }

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, fp);

    assert(img->IsTruecolor);

    cinfo.image_width = img->Width;
    cinfo.image_height = img->Height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    cinfo.optimize_coding = optimize;
    cinfo.dct_method = JDCT_ISLOW;

    /* Counter-intuitively, chroma sampling is specified relative to luma
    sampling, so we change the luma factors only (oversampling relative to
    chroma). */
    cinfo.comp_info[1].v_samp_factor = 1;
    cinfo.comp_info[1].h_samp_factor = 1;
    cinfo.comp_info[2].v_samp_factor = 1;
    cinfo.comp_info[2].h_samp_factor = 1;

    if (sampling < 0 || sampling > 2) {
        sampling = quality < 90 ? 2 : 0;
    }

    switch (sampling) {
        case 1:
            cinfo.comp_info[0].v_samp_factor = 1;
            cinfo.comp_info[0].h_samp_factor = 2;
            break;
        case 2:
            cinfo.comp_info[0].v_samp_factor = 2;
            cinfo.comp_info[0].h_samp_factor = 2;
            break;
        default:
            cinfo.comp_info[0].v_samp_factor = 1;
            cinfo.comp_info[0].h_samp_factor = 1;
            break;
    }

    jpeg_start_compress(&cinfo, TRUE);

    uint8_t *p = img->RasterData;
    while (cinfo.next_scanline < cinfo.image_height) {
        int i;
        for (i = 0; i < row_stride; i += 3) {
            /* Convert transparent pixels to white */
            row[i + 0] = p[3] ? p[0] : 255;
            row[i + 1] = p[3] ? p[1] : 255;
            row[i + 2] = p[3] ? p[2] : 255;
            p += 4;
        }
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);
    fclose(fp);
    jpeg_destroy_compress(&cinfo);
    free(row);
    return true;
}

static bool write_png(GifSplitImage *img, const char *filename)
{
    png_bytepp row_pointers;

    row_pointers = malloc(sizeof(*row_pointers) * img->Height);
    if (!row_pointers) {
        fprintf(stderr, "Out of memory\n");
        return false;
    }

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
    free(row_pointers);
    return true;
}

int main(int argc, char **argv)
{
    int opt;
    while ((opt = getopt(argc, argv, "hvVq:s:om:")) != -1) {
        switch (opt) {
        case 'v':
            verbose = 1;
            break;
        case 'V':
            fprintf(stderr, "gifsplit v"VERSION"\n");
            return 0;
        case 'q':
            jpeg = 1;
            quality = atoi(optarg);
            break;
        case 's':
            sampling = atoi(optarg);
            break;
        case 'o':
            optimize = true;
            break;
        case 'm':
            max_frames = atoi(optarg);
            break;
        default: /* 'h' */
            usage(argv[0]);
            return ERR_UNSPECIFIED;
        }
    }

    if (optind != (argc - 2)) {
        fprintf(stderr, "Expected 2 arguments after options\n");
        return ERR_UNSPECIFIED;
    }

    const char *in_filename = argv[optind];
    const char *output_base = argv[optind + 1];
    size_t fn_len = strlen(output_base) + 64;
    char *output_filename = malloc(fn_len + 1);
    if (!output_filename) {
        fprintf(stderr, "Out of memory\n");
        return ERR_UNSPECIFIED;
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
        return ERR_UNSPECIFIED;
    }

    GifSplitHandle *handle = GifSplitterOpen(gif);
    if (!handle) {
        fprintf(stderr, "Failed to greate GIF splitter handle\n");
        return ERR_UNSPECIFIED;
    }

    GifSplitImage *img;
    int frame = 0;

    while ((img = GifSplitterReadFrame(handle, jpeg))) {
        if (max_frames && frame >= max_frames) {
            fprintf(stderr, "Max frames exceeded\n");
            return ERR_MAX_FRAMES;
        }
        dbgprintf("Read frame %d (truecolor=%d, cmap=%d)\n", frame,
                  img->IsTruecolor, img->UsedLocalColormap);
        snprintf(output_filename, fn_len, "%s%06d.%s", output_base, frame,
                 jpeg ? "jpg" : "png");
        if (jpeg) {
            if (!write_jpeg(img, output_filename)) {
                fprintf(stderr, "Failed to write to %s\n", output_filename);
                return ERR_UNSPECIFIED;
            }
        } else {
            if (!write_png(img, output_filename)) {
                fprintf(stderr, "Failed to write to %s\n", output_filename);
                return ERR_UNSPECIFIED;
            }
        }
        printf("%d delay=%d\n", frame, img->DelayTime);
        frame++;
    }

    GifSplitInfo *info;
    info = GifSplitterGetInfo(handle);
    if (info->HasErrors) {
        fprintf(stderr, "Error while processing input gif\n");
        return ERR_UNSPECIFIED;
    }
    if (info)
        printf("loops=%d\n", info->LoopCount);

    GifSplitterClose(handle);
    free(output_filename);
    return 0;
}
