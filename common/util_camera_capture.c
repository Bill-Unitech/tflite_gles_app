/* ------------------------------------------------ *
 * The MIT License (MIT)
 * Copyright (c) 2019 terryky1220@gmail.com
 * ------------------------------------------------ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "util_v4l2.h"
#include "util_debug.h"
#include "util_texture.h"
#include "util_camera_capture.h"

static pthread_t    s_capture_thread;
static void         *s_capture_buf = NULL;
static capture_dev_t *s_cap_dev;
static int          s_capture_w, s_capture_h;
static int          s_capcrop_w, s_capcrop_h;
static int          s_capcropped = 0;
static unsigned int s_capture_fmt;
static int          s_force_convert_to_rgba = 0;

#define _max(A, B)    ((A) > (B) ? (A) : (B))
#define _min(A, B)    ((A) < (B) ? (A) : (B))


static int
convert_to_rgba8888 (void *buf, int ofstx, int ofsty, int cap_w, int cap_h, unsigned int fmt)
{
    int x, y;

    if (s_capture_buf == NULL)
    {
        s_capture_buf = (unsigned char *)malloc (cap_w * cap_h * 4);
    }

    if (fmt == v4l2_fourcc ('Y', 'U', 'Y', 'V') ||
        fmt == v4l2_fourcc ('U', 'Y', 'V', 'Y'))
    {
        unsigned char *src8 = buf;
        unsigned char *srcline = buf;
        unsigned char *dst8 = s_capture_buf;
        int y0_idx = 0, cb_idx = 1, y1_idx = 2, cr_idx = 3;

        if (fmt == v4l2_fourcc ('U', 'Y', 'V', 'Y'))
        {
            y0_idx = 1;
            cb_idx = 0;
            y1_idx = 3;
            cr_idx = 2;
        }
        for (y = 0; y < cap_h; y ++)
        {
            src8 = &srcline[y * 2 * s_capture_w];
            src8 += ofstx * 2;
            for (x = 0; x < cap_w; x += 2)
            {
                int y0 = src8[y0_idx];
                int cb = src8[cb_idx];
                int y1 = src8[y1_idx];
                int cr = src8[cr_idx];
                src8 += 4;

                y0 -= 16;
                y1 -= 16;
                cb -= 128;
                cr -= 128;
                int r, g, b;
                
                r = 1164 * y0 + 1596 * cr;
                g = 1164 * y0 -  392 * cb - 813 * cr;
                b = 1164 * y0 + 2017 * cb;
                
                r = _min (_max (r, 999) / 1000, 255);
                g = _min (_max (g, 999) / 1000, 255);
                b = _min (_max (b, 999) / 1000, 255);
                
                *dst8 ++ = r;
                *dst8 ++ = g;
                *dst8 ++ = b;
                *dst8 ++ = 255;

                r = 1164 * y1 + 1596 * cr;
                g = 1164 * y1 -  392 * cb - 813 * cr;
                b = 1164 * y1 + 2017 * cb;
                
                r = _min (_max (r, 999) / 1000, 255);
                g = _min (_max (g, 999) / 1000, 255);
                b = _min (_max (b, 999) / 1000, 255);
                
                *dst8 ++ = r;
                *dst8 ++ = g;
                *dst8 ++ = b;
                *dst8 ++ = 255;
            }
        }
    }
    else
    {
        fprintf (stderr, "ERR: %s(%d): pixformat(%.4s) is not supported.\n",
            __FILE__, __LINE__, (char *)&fmt);
        return -1;
    }
    return 0;
}

static int
copy_yuyv_image_cropped (void *buf, int ofstx, int ofsty, int cap_w, int cap_h, unsigned int fmt)
{
    if (s_capture_buf == NULL)
    {
        s_capture_buf = (unsigned char *)malloc (cap_w * cap_h * 2);
    }

    if (fmt == v4l2_fourcc ('Y', 'U', 'Y', 'V') ||
        fmt == v4l2_fourcc ('U', 'Y', 'V', 'Y'))
    {
        unsigned char *src8 = buf;
        unsigned char *dst8 = s_capture_buf;
        for (int ydst = 0; ydst < cap_h; ydst ++)
        {
            int ysrc = ydst + ofsty;
            unsigned char *srcline = &src8[ysrc * 2 * s_capture_w];
            unsigned char *dstline = &dst8[ydst * 2 * cap_w];

            srcline += ofstx * 2;

            memcpy (dstline, srcline, cap_w * 2);
        }
    }
    else
    {
        fprintf (stderr, "ERR: %s(%d): pixformat(%.4s) is not supported.\n",
            __FILE__, __LINE__, (char *)&fmt);
        return -1;
    }
    return 0;
}

static int
copy_yuyv_image (void *buf, int cap_w, int cap_h, unsigned int fmt)
{
    if (s_capture_buf == NULL)
    {
        s_capture_buf = (unsigned char *)malloc (cap_w * cap_h * 2);
    }

    if (fmt == v4l2_fourcc ('Y', 'U', 'Y', 'V') ||
        fmt == v4l2_fourcc ('U', 'Y', 'V', 'Y'))
    {
        memcpy (s_capture_buf, buf, cap_w * cap_h * 2);
    }
    else
    {
        fprintf (stderr, "ERR: %s(%d): pixformat(%.4s) is not supported.\n",
            __FILE__, __LINE__, (char *)&fmt);
        return -1;
    }
    return 0;
}

static void *
capture_thread_main ()
{
    v4l2_start_capture (s_cap_dev);

    while (1)
    {
        int ofstx = (s_capture_w - s_capcrop_w) * 0.5f;
        int ofsty = (s_capture_h - s_capcrop_h) * 0.5f;

        capture_frame_t *frame = v4l2_acquire_capture_frame (s_cap_dev);

        if (s_force_convert_to_rgba)
        {
            convert_to_rgba8888 (frame->vaddr, ofstx, ofsty, s_capcrop_w, s_capcrop_h, s_capture_fmt);
        }
        else
        {
            if (s_capcropped)
                copy_yuyv_image_cropped (frame->vaddr, ofstx, ofsty, s_capcrop_w, s_capcrop_h, s_capture_fmt);
            else
                copy_yuyv_image (frame->vaddr, s_capcrop_w, s_capcrop_h, s_capture_fmt);
        }
        v4l2_release_capture_frame (s_cap_dev, frame);
    }
    return 0;
}


int
init_capture (uint32_t flags)
{
    int cap_devid = -1;
    capture_dev_t *cap_dev;
    int cap_w, cap_h;
    unsigned int cap_fmt;

    cap_dev = v4l2_open_capture_device (cap_devid);
    if (cap_dev == NULL)
    {
        fprintf (stderr, "capture device not found.\n");
        return -1;
    }

    v4l2_get_capture_wh (cap_dev, &cap_w, &cap_h);
    v4l2_get_capture_pixelformat (cap_dev, &cap_fmt);

    v4l2_show_current_capture_settings (cap_dev);

    s_cap_dev     = cap_dev;
    s_capture_fmt = cap_fmt;
    s_capture_w = cap_w;
    s_capture_h = cap_h;

    if (flags & CAPTURE_SQUARED_CROP)
    {
        if (cap_w > cap_h)
            s_capcrop_w = s_capcrop_h = cap_h;
        else
            s_capcrop_w = s_capcrop_h = cap_w;
        s_capcropped = 1;
    }
    else
    {
        s_capcrop_w = cap_w;
        s_capcrop_h = cap_h;
    }

    if (flags & CAPTURE_PIXFORMAT_RGBA)
    {
        s_force_convert_to_rgba = 1;
    }
    
    return 0;
}

int 
get_capture_dimension (int *width, int *height)
{
    *width  = s_capcrop_w;
    *height = s_capcrop_h;

    return 0;
}

int 
get_capture_pixformat (uint32_t *pixformat)
{
    if (s_force_convert_to_rgba)
    {
        *pixformat = pixfmt_fourcc('R', 'G', 'B', 'A');
    }
    else
    {
        if (s_capture_fmt == v4l2_fourcc ('Y', 'U', 'Y', 'V'))
            *pixformat = pixfmt_fourcc('Y', 'U', 'Y', 'V');
        else if (s_capture_fmt == v4l2_fourcc ('U', 'Y', 'V', 'Y'))
            *pixformat = pixfmt_fourcc('U', 'Y', 'V', 'Y');
        else
        {
            fprintf (stderr, "ERR: %s(%d): pixformat(%.4s) is not supported.\n",
                __FILE__, __LINE__, (char *)&s_capture_fmt);
            return -1;
        }
    }
    return 0;
}

int
get_capture_buffer (void ** buf)
{
    *buf = s_capture_buf;
    return 0;
}

int
start_capture ()
{
    pthread_create (&s_capture_thread, NULL, capture_thread_main, NULL);
    return 0;
}

