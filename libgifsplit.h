#ifndef LIBGIFSPLIT_H
#define LIBGIFSPLIT_H

#include <stdbool.h>
#include <stdint.h>
#include <gif_lib.h>

struct GifSplitHandle_t;
typedef struct GifSplitHandle_t GifSplitHandle;

#define GIF_DISPOSAL_NONE 1
#define GIF_DISPOSAL_BACKGROUND 2
#define GIF_DISPOSAL_PREVIOUS 3

typedef uint16_t GifSize;

typedef struct GifSplitImage_t {
    GifSize Width, Height;      /* Always the same as the GifFileType's SWidth
                                   and SHeight */
    bool IsTruecolor;           /* Is this a truecolor frame (>255 colors?) */
    ColorMapObject *ColorMap;   /* Colormap for this frame, NULL if truecolor */
    GifWord TransparentColorIndex; /* Transparent color index, or -1 if none */
    GifPixelType *RasterData;   /* Raster data, one byte per pixel if ColorMap
                                   is present, or four bytes per pixel (RGBA) if
                                   IsTruecolor is true. The number of pixels
                                   is equal to the Width * Height */
    GifWord DelayTime;          /* Delay time for this frame, in 1/100s units */
    bool UsedLocalColormap;     /* Whether this image used a local colormap */

} GifSplitImage;

typedef struct GifSplitInfo_t {
    int LoopCount;              /* Number of times the animation should loop.
                                   0 means loop forever. */
    bool HasErrors;             /* Whether any errors occured while processing
                                   the image */
} GifSplitInfo;

/*
 * Initialize a GIF Splitter context.
 *
 * Given a just-opened gif_lib GifFileType context, create a GIF Splitter
 * context. Returns NULL if an error occured.
 */
GifSplitHandle *GifSplitterOpen(GifFileType *gif);

/*
 * Release a GIF Splitter context.
 *
 * Frees a GIF Splitter context, including all referenced buffers. The
 * underlying GifFileType context is also closed and freed.
 */
void GifSplitterClose(GifSplitHandle *handle);

/*
 * Get global information about the GIF file.
 *
 * This should be called after reading one frame, or preferably after reading
 * all frames, to ensure that all available info has been read and parsed.
 */
GifSplitInfo *GifSplitterGetInfo(GifSplitHandle *handle);

/*
 * Fetch a frame from the source GIF
 *
 * Retrieves the next frame from an open GIF Splitter context. The buffers for
 * the frame belong to the GIF Splitter context and will be reused on
 * subsequent calls to GifSplitterReadFrame. The caller must copy any frames
 * that it wishes to preserve, and must not attempt to free the structure
 * returned or its buffers.
 *
 * The returned image comprises the entire canvas area of the gif as it should
 * be displayed at a particular frame. Its dimensions are the screen dimensions
 * as specified in the GifFileType object. The disposal is provided for
 * informational purposes only.
 *
 * Returns NULL if an error occured.
 */
GifSplitImage *GifSplitterReadFrame(GifSplitHandle *handle);

#endif
