#define _POSIX_C_SOURCE 2

#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <png.h>


#include "libgifsplit.h"

int verbose = 0;

void usage(const char *argv0)
{
    fprintf(stderr, "Usage: %s [-v] input.gif output_base\n", argv0);
}

void dbgprintf(const char *fmt, ...) {
    va_list ap;

    if (!verbose)
        return;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
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

    while ((img = GifSplitterReadFrame(handle))) {
        dbgprintf("Read frame %d (truecolor=%d, cmap=%d)\n", frame,
                  img->IsTruecolor, img->UsedLocalColormap);
        frame++;
    }

    GifSplitInfo *info;
    info = GifSplitterGetInfo(handle);
    if (info->HasErrors) {
        fprintf(stderr, "Error while processing input gif\n");
        return 1;
    }
    if (info)
        printf("Loop flag: %d\n", info->LoopCount);

    GifSplitterClose(handle);
    return 0;
}
