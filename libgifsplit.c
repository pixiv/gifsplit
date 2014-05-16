#include "libgifsplit.h"
#include <malloc.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

/* Sanity/safety limit: no gifs larger than 10 megapixels per frame */
#define MAX_FRAME_SIZE 10000000

struct GifSplitHandle_t {
    GifFileType *File;
    GifPixelType *ReadBuf;
    GifImageDesc PrevImage;
    GifWord PrevDisposal;
    bool PrevFull;
    GifSplitImage *Canvas;
    GifSplitImage *PrevCanvas;
    GifSplitInfo Info;
};

static int InterlacedOffset[] = { 0, 4, 2, 1 };
static int InterlacedJumps[] = { 8, 8, 4, 2 };

static size_t GetImageSize(GifSplitImage *image) {
    size_t raster_bytes = (size_t)image->Width * (size_t)image->Height;
    if (image->IsTruecolor)
        raster_bytes *= 4;
    return raster_bytes;
}

static GifSplitImage *AllocImage(GifWord width, GifWord height, bool truecolor)
{
    GifSplitImage *img = malloc(sizeof(GifSplitImage));
    if (!img)
        return NULL;
    memset(img, 0, sizeof(*img));

    img->IsTruecolor = truecolor;
    img->Width = width;
    img->Height = height;
    img->RasterData = malloc(GetImageSize(img));
    if (!img->RasterData) {
        free(img);
        return NULL;
    }
    return img;
}

static void FreeImage(GifSplitImage *image)
{
    if (!image)
        return;
    if (image->RasterData) {
        free(image->RasterData);
        image->RasterData = NULL;
    }
    if (image->ColorMap) {
        FreeMapObject(image->ColorMap);
        image->ColorMap = NULL;
    }
    free(image);
}

static bool ReplaceColorMap(GifSplitImage *dst, ColorMapObject *map)
{
    if (dst->ColorMap)
        FreeMapObject(dst->ColorMap);
    dst->ColorMap = MakeMapObject(map->ColorCount, map->Colors);
    if (!dst->ColorMap)
        return false;
    return true;
}

static GifSplitImage *CloneImage(GifSplitImage *src)
{
    GifSplitImage *dst;

    dst = AllocImage(src->Width, src->Height, src->IsTruecolor);
    if (!dst)
        return NULL;

    if (!src->ColorMap) {
        dst->ColorMap = NULL;
    } else {
        if (!ReplaceColorMap(dst, src->ColorMap))
            return NULL;
    }

    memcpy(dst->RasterData, src->RasterData, GetImageSize(dst));
    dst->TransparentColorIndex = src->TransparentColorIndex;
    dst->DelayTime = src->DelayTime;
    return dst;
}

static bool ToTruecolor(GifSplitImage *image)
{
    if (image->IsTruecolor)
        return true;

    GifPixelType *new_data;
    new_data = malloc(image->Width * image->Height * 4);
    if (!new_data)
        return false;
    size_t pixels = (size_t)image->Width * (size_t)image->Height;

    GifPixelType *src, *dst;
    ColorMapObject *map = image->ColorMap;
    if (!map)
        return false;
    src = image->RasterData;
    dst = new_data;
    while (pixels--) {
        GifColorType color = {0,0,0};
        if (*src < map->ColorCount)
            color = map->Colors[*src];
        *dst++ = color.Red;
        *dst++ = color.Green;
        *dst++ = color.Blue;
        *dst++ = (*src == image->TransparentColorIndex) ? 0 : 255;
        src++;
    }
    image->IsTruecolor = true;
    free(image->RasterData);
    image->RasterData = new_data;
    image->TransparentColorIndex = -1;
    return true;
}

GifSplitHandle *GifSplitterOpen(GifFileType *gif)
{
    if (gif->SWidth <= 0 || gif->SHeight <= 0
        || ((size_t)gif->SWidth * (size_t)gif->SHeight) > MAX_FRAME_SIZE) {
        return NULL;
    }

    GifSplitHandle *handle = malloc(sizeof(GifSplitHandle));
    if (!handle)
        return NULL;

    memset(handle, 0, sizeof(*handle));

    handle->File = gif;
    handle->Info.LoopCount = 1;

    handle->ReadBuf = malloc(gif->SWidth * gif->SHeight);
    if (!handle->ReadBuf) {
        free(handle);
        return NULL;
    }

    handle->Canvas = AllocImage(gif->SWidth, gif->SHeight, false);
    if (!handle->Canvas) {
        free(handle->ReadBuf);
        free(handle);
        return NULL;
    }

    /* The canvas will normally be replaced by the first image entirely,
     * but if it isn't, the rest of the pixels should be transparent. We don't
     * know what the transparent color index is yet, so fake it by setting the
     * "previous image" dimensions to the entire canvas and disposal to
     * BACKGROUND, which will force GifSplitterReadFrame to do the right thing.
     */
    handle->PrevImage.Left = 0;
    handle->PrevImage.Top = 0;
    handle->PrevImage.Width = gif->SWidth;
    handle->PrevImage.Height = gif->SHeight;
    handle->PrevFull = true;
    handle->PrevDisposal = GIF_DISPOSAL_BACKGROUND;

    return handle;
}

void GifSplitterClose(GifSplitHandle *handle)
{
    FreeImage(handle->Canvas);
    FreeImage(handle->PrevCanvas);
    free(handle->ReadBuf);
    DGifCloseFile(handle->File);
    free(handle);
}

GifSplitInfo *GifSplitterGetInfo(GifSplitHandle *handle)
{
    return &handle->Info;
}

GifSplitImage *GifSplitterReadFrame(GifSplitHandle *handle)
{
    GifWord transparent_color_index = -1;
    GifWord disposal = GIF_DISPOSAL_NONE;

    GifRecordType record_type;

    /* Handle extension records and save their data */
    for(;;) {
        if (DGifGetRecordType(handle->File, &record_type) == GIF_ERROR)
            goto fail;

        if (record_type == TERMINATE_RECORD_TYPE) {
            return NULL;
        } else if (record_type == EXTENSION_RECORD_TYPE) {
            int ext_code;
            GifByteType *ext_data;
            if (DGifGetExtension(handle->File, &ext_code, &ext_data)
                == GIF_ERROR)
                goto fail;

            if (ext_code == GRAPHICS_EXT_FUNC_CODE && ext_data[0] == 4) {
                disposal = (ext_data[1] >> 2) & 7;
                if (disposal < GIF_DISPOSAL_NONE
                    || disposal > GIF_DISPOSAL_PREVIOUS)
                    disposal = GIF_DISPOSAL_NONE;

                handle->Canvas->DelayTime = ext_data[2] | (ext_data[3] << 8);

                if (ext_data[1] & 1)
                    transparent_color_index = ext_data[4];

            } else if (ext_code == APPLICATION_EXT_FUNC_CODE
                        && ext_data[0] == 11
                        && !memcmp(&ext_data[1], "NETSCAPE2.0", 11)) {
                if (DGifGetExtensionNext(handle->File, &ext_data)
                    == GIF_ERROR)
                    goto fail;
                if (ext_data && ext_data[0] == 3 && ext_data[1] == 1) {
                    handle->Info.LoopCount = ext_data[2] | (ext_data[3] << 8);
                }
            }
            while (ext_data) {
                if (DGifGetExtensionNext(handle->File, &ext_data)
                    == GIF_ERROR)
                    goto fail;
            }
            continue;
        } else if (record_type == IMAGE_DESC_RECORD_TYPE) {
            break;
        }
    }

    /* Got an image record */
    if (DGifGetImageDesc(handle->File) == GIF_ERROR)
        goto fail;

    GifImageDesc *gif_img = &handle->File->Image;

    bool is_full = false;

    if (gif_img->Top == 0 && gif_img->Left == 0
        && gif_img->Width == handle->File->SWidth
        && gif_img->Height == handle->File->SHeight)
        is_full = true;

    /* Sanity check */
    if (gif_img->Top < 0 || gif_img->Left < 0
        || gif_img->Width < 0 || gif_img->Height < 0
        || (gif_img->Left + gif_img->Width) > handle->File->SWidth
        || (gif_img->Top + gif_img->Height) > handle->File->SHeight)
        goto fail;

    /* Need to merge if the image is not the whole canvas, or it has
    transparent holes. */
    bool merge = !is_full || transparent_color_index != -1;

    if (handle->PrevDisposal == GIF_DISPOSAL_PREVIOUS) {
        FreeImage(handle->Canvas);
        handle->Canvas = handle->PrevCanvas;
        handle->PrevCanvas = NULL;
    } else if (handle->PrevDisposal == GIF_DISPOSAL_BACKGROUND) {
        /* Really means clear to transparent, these days. */
        if (handle->PrevFull) {
            /* No need to merge - replacing the entire image, including
            transparency if any. */
            merge = false;
            /* On top of that, if the next disposal is previous, change it to
             * background (which is equivalent since the previous image is
             * fully transparent). This avoids work below and a potential switch
             * to truecolor. */
            if (disposal == GIF_DISPOSAL_PREVIOUS)
                disposal = GIF_DISPOSAL_BACKGROUND;
        }
        /* Only bother disposing if we're merging OR if we need the canvas
         around for previous disposal of the current frame. */
        if (merge || disposal == GIF_DISPOSAL_PREVIOUS) {
            if (handle->Canvas->TransparentColorIndex == -1) {
                /* Evil! Need a transparent background but no transparent index.
                Punt and switch to truecolor mode. */
                if (!ToTruecolor(handle->Canvas))
                    goto fail;
            }
            GifPixelType clear_value = (handle->Canvas->IsTruecolor ? 0 :
                                        handle->Canvas->TransparentColorIndex);
            int pixel_size = handle->Canvas->IsTruecolor ? 4 : 1;

            GifPixelType *p = (handle->Canvas->RasterData
                               + pixel_size * (handle->PrevImage.Left
                                               + handle->Canvas->Width
                                               * handle->PrevImage.Top));
            for (int y = 0; y < handle->PrevImage.Height; y++) {
                memset(p, clear_value, handle->PrevImage.Width * pixel_size);
                p += handle->Canvas->Width * pixel_size;
            }
        }
    }

    /* Save a copy of the canvas if we need to dispose to previous */
    if (disposal == GIF_DISPOSAL_PREVIOUS) {
        FreeImage(handle->PrevCanvas);
        handle->PrevCanvas = CloneImage(handle->Canvas);
        if (!handle->PrevCanvas)
            goto fail;
    }

    GifPixelType *p = handle->ReadBuf;

    /* Deinterlace image, if necessary */
    if (gif_img->Interlace) {
        for (int i = 0; i < 4; i++) {
            for (int y = InterlacedOffset[i]; y < gif_img->Height;
                y += InterlacedJumps[i]) {
                if (DGifGetLine(handle->File, p + gif_img->Width * y,
                                gif_img->Width) == GIF_ERROR)
                    goto fail;
            }
        }
    } else {
        for (int y = 0; y < gif_img->Height; y++) {
            if (DGifGetLine(handle->File, p + gif_img->Width * y,
                            gif_img->Width) == GIF_ERROR)
                goto fail;
        }
    }

    ColorMapObject *gif_map = gif_img->ColorMap;
    if (!gif_map) {
        handle->Canvas->UsedLocalColormap = false;
        gif_map = handle->File->SColorMap;
        if (!gif_map)
            goto fail;
    } else {
        handle->Canvas->UsedLocalColormap = true;
    }

    /* Now apply it to the canvas */
    if (!merge) {
        /* The easy case: no merging */
        if (is_full) {
            /* Easy, just copy everything */
            handle->Canvas->IsTruecolor = false;
            memcpy(handle->Canvas->RasterData, p,
                   gif_img->Width * gif_img->Height);
            if (!ReplaceColorMap(handle->Canvas, gif_map))
                goto fail;
            handle->Canvas->TransparentColorIndex = transparent_color_index;
        } else {
            if (transparent_color_index == -1) {
                /* Evil! Need transparent padding but no transparent color.
                Punt and switch to truecolor, then perform a truecolor merge. */
                if (!handle->Canvas->IsTruecolor) {
                    FreeImage(handle->Canvas);
                    handle->Canvas = AllocImage(handle->File->SWidth,
                                                handle->File->SHeight, true);
                }
                memset(handle->Canvas->RasterData, 0,
                       GetImageSize(handle->Canvas));
                merge = true;
            } else {
                /* Reset the canvas to transparent and copy the subimage */
                handle->Canvas->IsTruecolor = false;
                memset(handle->Canvas->RasterData, transparent_color_index,
                       GetImageSize(handle->Canvas));
                GifPixelType *q = (handle->Canvas->RasterData + gif_img->Left
                                   + gif_img->Top * handle->Canvas->Width);
                for (int y = 0; y < gif_img->Height; y++) {
                    memcpy(q, p, gif_img->Width);
                    q += handle->Canvas->Width;
                    p += gif_img->Width;
                }
                if (!ReplaceColorMap(handle->Canvas, gif_map))
                    goto fail;
                handle->Canvas->TransparentColorIndex = transparent_color_index;
            }
        }
    }

    if (merge) {
        if (!handle->Canvas->IsTruecolor) {
            assert(handle->Canvas->ColorMap);
            ColorMapObject *canvas_map = handle->Canvas->ColorMap;
            if (canvas_map->ColorCount != gif_map->ColorCount
                || memcmp(canvas_map->Colors, gif_map->Colors,
                          sizeof(GifColorType) * gif_map->ColorCount)
                || (handle->Canvas->TransparentColorIndex
                    != transparent_color_index)) {
                /* Colormaps differ. We could attempt to merge them if
                possible, but for now, let's just punt to truecolor mode. */
                if (!ToTruecolor(handle->Canvas))
                    goto fail;
            } else {
                /* Same colormaps, so we can just merge */
                GifPixelType *q = (handle->Canvas->RasterData + gif_img->Left
                                   + gif_img->Top * handle->Canvas->Width);
                for (int y = 0; y < gif_img->Height; y++) {
                    for (int x = 0; x < gif_img->Width; x++) {
                        if (*p != transparent_color_index)
                            *q = *p;
                        q++;
                        p++;
                    }
                    q += handle->Canvas->Width - gif_img->Width;
                }
            }
        }
        if (handle->Canvas->IsTruecolor) {
            GifPixelType *q = (handle->Canvas->RasterData + 4 * gif_img->Left
                               + 4 * gif_img->Top * handle->Canvas->Width);
            for (int y = 0; y < gif_img->Height; y++) {
                for (int x = 0; x < gif_img->Width; x++) {
                    if (*p != transparent_color_index) {
                        GifColorType color = {0,0,0};
                        if (*p < gif_map->ColorCount)
                            color = gif_map->Colors[*p];
                        *q++ = color.Red;
                        *q++ = color.Green;
                        *q++ = color.Blue;
                        *q++ = 255;
                    } else {
                        q += 4;
                    }
                    p++;
                }
                q += (handle->Canvas->Width - gif_img->Width) * 4;
            }
        }
    }

    handle->PrevDisposal = disposal;
    handle->PrevImage = *gif_img;
    handle->PrevFull = is_full;

    return handle->Canvas;

fail:
    handle->Info.HasErrors = true;
    return NULL;
}
