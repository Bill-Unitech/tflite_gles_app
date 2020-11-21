/* ------------------------------------------------ *
 * The MIT License (MIT)
 * Copyright (c) 2019 terryky1220@gmail.com
 * ------------------------------------------------ */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <float.h>
#include <GLES2/gl2.h>
#include "util_egl.h"
#include "util_debugstr.h"
#include "util_pmeter.h"
#include "util_texture.h"
#include "util_render2d.h"
#include "util_matrix.h"
#include "tflite_hair_segmentation.h"
#include "render_hair.h"
#include "util_camera_capture.h"
#include "util_video_decode.h"

#define UNUSED(x) (void)(x)





/* resize image to DNN network input size and convert to fp32. */
void
feed_segmentation_image (texture_2d_t *srctex, int win_w, int win_h)
{
    int x, y, w, h;
    float *buf_fp32 = (float *)get_segmentation_input_buf (&w, &h);
    unsigned char *buf_ui8 = NULL;
    static unsigned char *pui8 = NULL;

    if (pui8 == NULL)
        pui8 = (unsigned char *)malloc(w * h * 4);

    buf_ui8 = pui8;

    draw_2d_texture_ex (srctex, 0, win_h - h, w, h, 1);

    glPixelStorei (GL_PACK_ALIGNMENT, 4);
    glReadPixels (0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf_ui8);

    /* convert UI8 [0, 255] ==> FP32 [0, 1] */
    float mean =   0.0f;
    float std  = 255.0f;
    for (y = 0; y < h; y ++)
    {
        for (x = 0; x < w; x ++)
        {
            int r = *buf_ui8 ++;
            int g = *buf_ui8 ++;
            int b = *buf_ui8 ++;
            buf_ui8 ++;          /* skip alpha */
            *buf_fp32 ++ = (float)(r - mean) / std;
            *buf_fp32 ++ = (float)(g - mean) / std;
            *buf_fp32 ++ = (float)(b - mean) / std;
            *buf_fp32 ++ = 0.0f;
        }
    }

    return;
}

static void 
colormap_hsv (float h, float col[4])
{
    float s = 1.0f;
    float v = 1.0f;

    float r = v;
    float g = v;
    float b = v;

    h *= 6.0f;
    int i = (int) h;
    float f = h - (float) i;

    switch (i) 
    {
    default:
    case 0:
        g *= 1 - s * (1 - f);
        b *= 1 - s;
        break;
    case 1:
        r *= 1 - s * f;
        b *= 1 - s;
        break;
    case 2:
        r *= 1 - s;
        b *= 1 - s * (1 - f);
        break;
    case 3:
        r *= 1 - s;
        g *= 1 - s * f;
        break;
    case 4:
        r *= 1 - s * (1 - f);
        g *= 1 - s;
        break;
    case 5:
        g *= 1 - s;
        b *= 1 - s * f;
        break;
    }

    col[0] = r;
    col[1] = g;
    col[2] = b;
    col[3] = 1.0f;
}

static void
render_hsv_circle (int x0, int y0, float angle)
{
    float col[4];
    float r = 80.0f;

    float gray[] = {0.0f, 0.0f, 0.0f, 0.2f};
    draw_2d_fillrect (x0 - r - 10, y0 - r - 10, 2 * r + 20, 2 * r + 20, gray);

    for (int i = 0; i < 360; i ++)
    {
        int x1 = x0 + (int)(r * cosf (DEG_TO_RAD(i)) + 0.5f);
        int y1 = y0 + (int)(r * sinf (DEG_TO_RAD(i)) + 0.5f);

        colormap_hsv ((float)i /360.0f, col);
        draw_2d_line (x0, y0, x1, y1, col, 2);
    }

    float col_white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    int x1 = x0 + (int)(r * cosf (DEG_TO_RAD(angle)) + 0.5f);
    int y1 = y0 + (int)(r * sinf (DEG_TO_RAD(angle)) + 0.5f);
    draw_2d_line (x0, y0, x1, y1, col_white, 2);

    int q = 10;
    draw_2d_rect (x1 - (q/2), y1 - (q/2), q, q, col_white, 2);
}

//#define RENDER_BY_BLEND 1
void
render_segment_result (int ofstx, int ofsty, int draw_w, int draw_h, 
                       texture_2d_t *srctex, segmentation_result_t *segment_ret)
{
    float *segmap = segment_ret->segmentmap;
    int segmap_w  = segment_ret->segmentmap_dims[0];
    int segmap_h  = segment_ret->segmentmap_dims[1];
    int segmap_c  = segment_ret->segmentmap_dims[2];
    int x, y, c;
    unsigned int imgbuf[segmap_h][segmap_w];
    float hair_color[4] = {0};
    float back_color[4] = {0};
    static float s_hsv_h = 0.0f;

    s_hsv_h += 5.0f;
    if (s_hsv_h >= 360.0f)
        s_hsv_h = 0.0f;

    colormap_hsv (s_hsv_h / 360.0f, hair_color);

#if defined (RENDER_BY_BLEND)
    float lumi = (hair_color[0] * 0.299f + hair_color[1] * 0.587f + hair_color[2] * 0.114f);
    hair_color[3] = lumi;
#endif

    /* find the most confident class for each pixel. */
    for (y = 0; y < segmap_h; y ++)
    {
        for (x = 0; x < segmap_w; x ++)
        {
            int max_id;
            float conf_max = 0;
            for (c = 0; c < MAX_SEGMENT_CLASS; c ++)
            {
                float confidence = segmap[(y * segmap_w * segmap_c)+ (x * segmap_c) + c];
                if (c == 0 || confidence > conf_max)
                {
                    conf_max = confidence;
                    max_id = c;
                }
            }

            float *col = (max_id > 0) ? hair_color : back_color;
            unsigned char r = ((int)(col[0] * 255)) & 0xff;
            unsigned char g = ((int)(col[1] * 255)) & 0xff;
            unsigned char b = ((int)(col[2] * 255)) & 0xff;
            unsigned char a = ((int)(col[3] * 255)) & 0xff;

            imgbuf[y][x] = (a << 24) | (b << 16) | (g << 8) | (r);
        }
    }

    GLuint texid;
    glGenTextures (1, &texid );
    glBindTexture (GL_TEXTURE_2D, texid);

    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glPixelStorei (GL_UNPACK_ALIGNMENT, 4);

    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA,
        segmap_w, segmap_h, 0, GL_RGBA,
        GL_UNSIGNED_BYTE, imgbuf);

#if !defined (RENDER_BY_BLEND)
    draw_colored_hair (srctex, texid, ofstx, ofsty, draw_w, draw_h, 0, hair_color);
#else
    draw_2d_texture_ex (srctex, ofstx, ofsty, draw_w, draw_h, 0);

    unsigned int blend_add  [] = {GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE};
    draw_2d_texture_blendfunc (texid, ofstx, ofsty, draw_w, draw_h, 0, blend_add);
#endif

    glDeleteTextures (1, &texid);

    render_hsv_circle (ofstx + draw_w - 100, ofsty + 100, s_hsv_h);
}


/* Adjust the texture size to fit the window size
 *
 *                      Portrait
 *     Landscape        +------+
 *     +-+------+-+     +------+
 *     | |      | |     |      |
 *     | |      | |     |      |
 *     +-+------+-+     +------+
 *                      +------+
 */
static void
adjust_texture (int win_w, int win_h, int texw, int texh, 
                int *dx, int *dy, int *dw, int *dh)
{
    float win_aspect = (float)win_w / (float)win_h;
    float tex_aspect = (float)texw  / (float)texh;
    float scale;
    float scaled_w, scaled_h;
    float offset_x, offset_y;

    if (win_aspect > tex_aspect)
    {
        scale = (float)win_h / (float)texh;
        scaled_w = scale * texw;
        scaled_h = scale * texh;
        offset_x = (win_w - scaled_w) * 0.5f;
        offset_y = 0;
    }
    else
    {
        scale = (float)win_w / (float)texw;
        scaled_w = scale * texw;
        scaled_h = scale * texh;
        offset_x = 0;
        offset_y = (win_h - scaled_h) * 0.5f;
    }

    *dx = (int)offset_x;
    *dy = (int)offset_y;
    *dw = (int)scaled_w;
    *dh = (int)scaled_h;
}


/*--------------------------------------------------------------------------- *
 *      M A I N    F U N C T I O N
 *--------------------------------------------------------------------------- */
int
main(int argc, char *argv[])
{
    char input_name_default[] = "assets/pakutaso.jpg";
    char *input_name = NULL;
    int count;
    int win_w = 800;
    int win_h = 600;
    int texid;
    int texw, texh, draw_x, draw_y, draw_w, draw_h;
    texture_2d_t captex = {0};
    double ttime[10] = {0}, interval, invoke_ms;
    int enable_camera = 1;
    UNUSED (argc);
    UNUSED (*argv);
#if defined (USE_INPUT_VIDEO_DECODE)
    int enable_video = 0;
#endif

    {
        int c;
        const char *optstring = "v:x";

        while ((c = getopt (argc, argv, optstring)) != -1)
        {
            switch (c)
            {
#if defined (USE_INPUT_VIDEO_DECODE)
            case 'v':
                enable_video = 1;
                input_name = optarg;
                break;
#endif
            case 'x':
                enable_camera = 0;
                break;
            }
        }

        while (optind < argc)
        {
            input_name = argv[optind];
            optind++;
        }
    }

    if (input_name == NULL)
        input_name = input_name_default;

    egl_init_with_platform_window_surface (2, 0, 0, 0, win_w * 2, win_h);

    init_2d_renderer (win_w, win_h);
    init_hair_renderer (win_w, win_h);
    init_pmeter (win_w, win_h, 500);
    init_dbgstr (win_w, win_h);
    init_tflite_segmentation ();

#if defined (USE_GL_DELEGATE) || defined (USE_GPU_DELEGATEV2)
    /* we need to recover framebuffer because GPU Delegate changes the FBO binding */
    glBindFramebuffer (GL_FRAMEBUFFER, 0);
    glViewport (0, 0, win_w, win_h);
#endif

#if defined (USE_INPUT_VIDEO_DECODE)
    /* initialize FFmpeg video decode */
    if (enable_video && init_video_decode () == 0)
    {
        create_video_texture (&captex, input_name);
        texw = captex.width;
        texh = captex.height;
        enable_camera = 0;
    }
    else
#endif
#if defined (USE_INPUT_CAMERA_CAPTURE)
    /* initialize V4L2 capture function */
    if (enable_camera && init_capture (CAPTURE_PIXFORMAT_RGBA) == 0)
    {
        create_capture_texture (&captex);
        texw = captex.width;
        texh = captex.height;
    }
    else
#endif
    {
        load_jpg_texture (input_name, &texid, &texw, &texh);
        captex.texid  = texid;
        captex.width  = texw;
        captex.height = texh;
        captex.format = pixfmt_fourcc ('R', 'G', 'B', 'A');
        enable_camera = 0;
    }
    adjust_texture (win_w, win_h, texw, texh, &draw_x, &draw_y, &draw_w, &draw_h);

    glClearColor (0.f, 0.f, 0.f, 1.0f);

    /* --------------------------------------- *
     *  Render Loop
     * --------------------------------------- */
    for (count = 0; ; count ++)
    {
        segmentation_result_t segment_result;
        char strbuf[512];

        PMETER_RESET_LAP ();
        PMETER_SET_LAP ();

        ttime[1] = pmeter_get_time_ms ();
        interval = (count > 0) ? ttime[1] - ttime[0] : 0;
        ttime[0] = ttime[1];

        glViewport (0, 0, win_w, win_h);
        glClear (GL_COLOR_BUFFER_BIT);

#if defined (USE_INPUT_VIDEO_DECODE)
        /* initialize FFmpeg video decode */
        if (enable_video)
        {
            update_video_texture (&captex);
        }
#endif
#if defined (USE_INPUT_CAMERA_CAPTURE)
        if (enable_camera)
        {
            update_capture_texture (&captex);
        }
#endif

        /* --------------------------------------- *
         *  hair segmentation
         * --------------------------------------- */
        feed_segmentation_image (&captex, win_w, win_h);

        ttime[2] = pmeter_get_time_ms ();
        invoke_segmentation (&segment_result);
        ttime[3] = pmeter_get_time_ms ();
        invoke_ms = ttime[3] - ttime[2];

        /* --------------------------------------- *
         *  render scene
         * --------------------------------------- */
        glClear (GL_COLOR_BUFFER_BIT);

        glViewport (0, 0, win_w, win_h);
        draw_2d_texture_ex (&captex, draw_x, draw_y, draw_w, draw_h, 0);

        /* visualize the segmentation results. */
        glViewport (win_w, 0, win_w, win_h);
        render_segment_result (draw_x, draw_y, draw_w, draw_h, &captex, &segment_result);

        /* --------------------------------------- *
         *  post process
         * --------------------------------------- */
        glViewport (0, 0, win_w, win_h);
        draw_pmeter (0, 40);

        sprintf (strbuf, "Interval:%5.1f [ms]\nTFLite  :%5.1f [ms]", interval, invoke_ms);
        draw_dbgstr (strbuf, 10, 10);

        egl_swap();
    }

    return 0;
}

