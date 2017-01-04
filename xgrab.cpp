#include "xgrab.h"
#include "Logger.h"
#include <iostream>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>

// Converts a X11 image format to a format that libav/ffmpeg understands.
static AVPixelFormat X11ImageGetPixelFormat(XImage* image) {
    switch(image->bits_per_pixel) {
        case 8: return AV_PIX_FMT_PAL8;
        case 16: {
            if(image->red_mask == 0xf800 && image->green_mask == 0x07e0 && image->blue_mask == 0x001f) return AV_PIX_FMT_RGB565;
            if(image->red_mask == 0x7c00 && image->green_mask == 0x03e0 && image->blue_mask == 0x001f) return AV_PIX_FMT_RGB555;
            break;
        }
        case 24: {
            if(image->red_mask == 0xff0000 && image->green_mask == 0x00ff00 && image->blue_mask == 0x0000ff) return AV_PIX_FMT_BGR24;
            if(image->red_mask == 0x0000ff && image->green_mask == 0x00ff00 && image->blue_mask == 0xff0000) return AV_PIX_FMT_RGB24;
            break;
        }
        case 32: {
            if(image->red_mask == 0xff0000 && image->green_mask == 0x00ff00 && image->blue_mask == 0x0000ff) return AV_PIX_FMT_BGRA;
            if(image->red_mask == 0x0000ff && image->green_mask == 0x00ff00 && image->blue_mask == 0xff0000) return AV_PIX_FMT_RGBA;
            if(image->red_mask == 0xff000000 && image->green_mask == 0x00ff0000 && image->blue_mask == 0x0000ff00) return AV_PIX_FMT_ABGR;
            if(image->red_mask == 0x0000ff00 && image->green_mask == 0x00ff0000 && image->blue_mask == 0xff000000) return AV_PIX_FMT_ARGB;
            break;
        }
    }
    fprintf(stderr,"error: x11 get pixelformat \n");
}

// clears a rectangular area of an image (i.e. sets the memory to zero, which will most likely make the image black)
static void X11ImageClearRectangle(XImage* image, unsigned int x, unsigned int y, unsigned int w, unsigned int h) {

    // check the image format
    if(image->bits_per_pixel % 8 != 0)
        return;
    unsigned int pixel_bytes = image->bits_per_pixel / 8;

    // fill the rectangle with zeros
    for(unsigned int j = 0; j < h; ++j) {
        uint8_t *image_row = (uint8_t*) image->data + image->bytes_per_line * (y + j);
        memset(image_row + pixel_bytes * x, 0, pixel_bytes * w);
    }

}

// Draws the current cursor at the current position on the image. Requires XFixes.
// Note: In the original code from x11grab, the variables for red and blue are swapped
// (which doesn't change the result, but it's confusing).
// Note 2: This function assumes little-endianness.
// Note 3: This function only supports 24-bit and 32-bit images (it does nothing for other bit depths).
static void X11ImageDrawCursor(Display* dpy, XImage* image, int recording_area_x, int recording_area_y) {

    // check the image format
    unsigned int pixel_bytes, r_offset, g_offset, b_offset;
    if(image->bits_per_pixel == 24 && image->red_mask == 0xff0000 && image->green_mask == 0x00ff00 && image->blue_mask == 0x0000ff) {
        pixel_bytes = 3;
        r_offset = 2; g_offset = 1; b_offset = 0;
    } else if(image->bits_per_pixel == 24 && image->red_mask == 0x0000ff && image->green_mask == 0x00ff00 && image->blue_mask == 0xff0000) {
        pixel_bytes = 3;
        r_offset = 0; g_offset = 1; b_offset = 2;
    } else if(image->bits_per_pixel == 32 && image->red_mask == 0xff0000 && image->green_mask == 0x00ff00 && image->blue_mask == 0x0000ff) {
        pixel_bytes = 4;
        r_offset = 2; g_offset = 1; b_offset = 0;
    } else if(image->bits_per_pixel == 32 && image->red_mask == 0x0000ff && image->green_mask == 0x00ff00 && image->blue_mask == 0xff0000) {
        pixel_bytes = 4;
        r_offset = 0; g_offset = 1; b_offset = 2;
    } else if(image->bits_per_pixel == 32 && image->red_mask == 0xff000000 && image->green_mask == 0x00ff0000 && image->blue_mask == 0x0000ff00) {
        pixel_bytes = 4;
        r_offset = 3; g_offset = 2; b_offset = 1;
    } else if(image->bits_per_pixel == 32 && image->red_mask == 0x0000ff00 && image->green_mask == 0x00ff0000 && image->blue_mask == 0xff000000) {
        pixel_bytes = 4;
        r_offset = 1; g_offset = 2; b_offset = 3;
    } else {
        return;
    }

    // get the cursor
    XFixesCursorImage *xcim = XFixesGetCursorImage(dpy);
    if(xcim == NULL)
        return;

    // calculate the position of the cursor
    int x = xcim->x - xcim->xhot - recording_area_x;
    int y = xcim->y - xcim->yhot - recording_area_y;

    // calculate the part of the cursor that's visible
    int cursor_left = std::max(0, -x), cursor_right = std::min((int) xcim->width, image->width - x);
    int cursor_top = std::max(0, -y), cursor_bottom = std::min((int) xcim->height, image->height - y);

    // draw the cursor
    // XFixesCursorImage uses 'long' instead of 'int' to store the cursor images, which is a bit weird since
    // 'long' is 64-bit on 64-bit systems and only 32 bits are actually used. The image uses premultiplied alpha.
    for(int j = cursor_top; j < cursor_bottom; ++j) {
        unsigned long *cursor_row = xcim->pixels + xcim->width * j;
        uint8_t *image_row = (uint8_t*) image->data + image->bytes_per_line * (y + j);
        for(int i = cursor_left; i < cursor_right; ++i) {
            unsigned long cursor_pixel = cursor_row[i];
            uint8_t *image_pixel = image_row + pixel_bytes * (x + i);
            int cursor_a = (uint8_t) (cursor_pixel >> 24);
            int cursor_r = (uint8_t) (cursor_pixel >> 16);
            int cursor_g = (uint8_t) (cursor_pixel >> 8);
            int cursor_b = (uint8_t) (cursor_pixel >> 0);
            if(cursor_a == 255) {
                image_pixel[r_offset] = cursor_r;
                image_pixel[g_offset] = cursor_g;
                image_pixel[b_offset] = cursor_b;
            } else {
                image_pixel[r_offset] = (image_pixel[r_offset] * (255 - cursor_a) + 127) / 255 + cursor_r;
                image_pixel[g_offset] = (image_pixel[g_offset] * (255 - cursor_a) + 127) / 255 + cursor_g;
                image_pixel[b_offset] = (image_pixel[b_offset] * (255 - cursor_a) + 127) / 255 + cursor_b;
            }
        }
    }

    // free the cursor
    XFree(xcim);

}

QSize CalculateScaledSize(QSize in, QSize out) {
    assert(in.width() > 0 && in.height() > 0);
    if(in.width() <= out.width() && in.height() <= out.height())
        return in;
    if(in.width() * out.height() > out.width() * in.height())
        return QSize(out.width(), (out.width() * in.height() + in.width() / 2) / in.width());
    else
        return QSize((out.height() * in.width() + in.height() / 2) / in.height(), out.height());
}

xgrab::xgrab(unsigned int x, unsigned int y, unsigned int width, unsigned int height, bool record_cursor, bool follow_cursor) {

    m_x = x;
    m_y = y;
    m_width = width;
    m_height = height;
    m_record_cursor = record_cursor;
    m_follow_cursor = follow_cursor;

    m_x11_display = NULL;
    m_x11_shm_info.shmid = -1;
    m_x11_shm_info.shmaddr = (char*) -1;
    m_x11_shm_server_attached = false;
    m_x11_image = NULL;
    m_sws_context = NULL;

    if(m_width == 0 || m_height == 0) {
        Logger::LogError("[X11Input::Init] " + Logger::tr("Error: Width or height is zero!"));
        throw X11Exception();
    }
    if(m_width > 10000 || m_height > 10000) {
        Logger::LogError("[X11Input::Init] " + Logger::tr("Error: Width or height is too large, the maximum width and height is %1!").arg(10000));
        throw X11Exception();
    }

    try {
        Init();
    } catch(...) {
        Free();
        throw;
    }

}

xgrab::~xgrab(){
    Free();
}

void xgrab::Init() {

    // do the X11 stuff
    // we need a separate display because the existing one would interfere with what Qt is doing in some cases
    m_x11_display = XOpenDisplay(NULL); //QX11Info::display();
    if(m_x11_display == NULL) {
        fprintf(stderr, "error \n");
    }
    m_x11_screen = DefaultScreen(m_x11_display); //QX11Info::appScreen();
    m_x11_root = RootWindow(m_x11_display, m_x11_screen); //QX11Info::appRootWindow(m_x11_screen);
    m_x11_visual = DefaultVisual(m_x11_display, m_x11_screen); //(Visual*) QX11Info::appVisual(m_x11_screen);
    m_x11_depth = DefaultDepth(m_x11_display, m_x11_screen); //QX11Info::appDepth(m_x11_screen);
    m_x11_use_shm = XShmQueryExtension(m_x11_display);
    if(m_x11_use_shm) {
        fprintf(stderr, "use_shm \n");
        m_x11_image = XShmCreateImage(m_x11_display, m_x11_visual, m_x11_depth, ZPixmap, NULL, &m_x11_shm_info, m_width, m_height);
        if(m_x11_image == NULL) {
            fprintf(stderr, "error \n");
        }
        m_x11_shm_info.shmid = shmget(IPC_PRIVATE, m_x11_image->bytes_per_line * m_x11_image->height, IPC_CREAT | 0700);
        if(m_x11_shm_info.shmid == -1) {
            fprintf(stderr, "error \n");
        }
        m_x11_shm_info.shmaddr = m_x11_image->data = (char*) shmat(m_x11_shm_info.shmid, NULL, SHM_RND);
        if(m_x11_shm_info.shmaddr == (char*) -1) {
            fprintf(stderr, "error \n");
        }
        m_x11_shm_info.readOnly = false;
        // the server will attach later
    } else {
        fprintf(stderr, "not_use_shm \n");
    }

    // showing the cursor requires XFixes (which should be supported on any modern X server, but let's check it anyway)
    if(m_record_cursor) {
        int event, error;
        if(!XFixesQueryExtension(m_x11_display, &event, &error)) {
            fprintf(stderr, "error \n");
            m_record_cursor = false;
        }
    }

    // get screen configuration information, so we can replace the unused areas with black rectangles (rather than showing random uninitialized memory)
    // this also used by the mouse following code to make sure that the rectangle stays on the screen
    //connect(QApplication::desktop(), SIGNAL(screenCountChanged(int)), this, SLOT(UpdateScreenConfiguration()));
    //connect(QApplication::desktop(), SIGNAL(resized(int)), this, SLOT(UpdateScreenConfiguration()));
    //UpdateScreenConfiguration();

    // initialize frame counter
    //m_frame_counter = 0;
    //m_fps_last_timestamp = hrt_time_micro();
    //m_fps_last_counter = 0;
    //m_fps_current = 0.0;

    // start input thread
    //m_should_stop = false;
    //m_error_occurred = false;
    //m_thread = std::thread(&X11Input::InputThread, this);

}

void xgrab::Free() {
    if(m_x11_shm_server_attached) {
        XShmDetach(m_x11_display, &m_x11_shm_info);
        m_x11_shm_server_attached = false;
    }
    if(m_x11_shm_info.shmaddr != (char*) -1) {
        shmdt(m_x11_shm_info.shmaddr);
        m_x11_shm_info.shmaddr = (char*) -1;
    }
    if(m_x11_shm_info.shmid != -1) {
        shmctl(m_x11_shm_info.shmid, IPC_RMID, NULL);
        m_x11_shm_info.shmid = -1;
    }
    if(m_x11_image != NULL) {
        XDestroyImage(m_x11_image);
        m_x11_image = NULL;
    }
    if(m_x11_display != NULL) {
        XCloseDisplay(m_x11_display);
        m_x11_display = NULL;
    }
    if(m_sws_context != NULL){
        sws_freeContext(m_sws_context);
        m_sws_context = NULL;
    }
}

uint8_t * xgrab::Forpreview(){
    int grab_x = m_x;
    int grab_y = m_y;
    // get the image
    //fprintf(stderr,"Info : GetImage! \n");
    if(m_x11_use_shm) {
        if(!m_x11_shm_server_attached) {
            if(!XShmAttach(m_x11_display, &m_x11_shm_info)) {
                fprintf(stderr,"Error: Can't attach server to shared memory! \n");
            }
            m_x11_shm_server_attached = true;
        }
        if(!XShmGetImage(m_x11_display, m_x11_root, m_x11_image, grab_x, grab_y, AllPlanes)) {
            fprintf(stderr,"Error: Can't get image (using shared memory)!\n");
        }
    } else {
        if(m_x11_image != NULL) {
            XDestroyImage(m_x11_image);
            m_x11_image = NULL;
        }
        m_x11_image = XGetImage(m_x11_display, m_x11_root, grab_x, grab_y, m_width, m_height, AllPlanes, ZPixmap);
        if(m_x11_image == NULL) {
            fprintf(stderr,"Error: Can't get image (not using shared memory)!\n");
        }
    }

    // draw the cursor
    if(m_record_cursor) {
        X11ImageDrawCursor(m_x11_display, m_x11_image, grab_x, grab_y);
    }
    // push the frame
    uint8_t *image_data = (uint8_t*) m_x11_image->data;
    m_image_stride = m_x11_image->bytes_per_line;

    return image_data;
}

void xgrab::GetQimage(AVFrame *frame, AVPixelFormat out_pixel_format, QSize out_size){
    int grab_x = m_x;
    int grab_y = m_y;
    // get the image
    //fprintf(stderr,"Info : GetImage! \n");
    if(m_x11_use_shm) {
        if(!m_x11_shm_server_attached) {
            if(!XShmAttach(m_x11_display, &m_x11_shm_info)) {
                fprintf(stderr,"Error: Can't attach server to shared memory! \n");
            }
            m_x11_shm_server_attached = true;
        }
        if(!XShmGetImage(m_x11_display, m_x11_root, m_x11_image, grab_x, grab_y, AllPlanes)) {
            fprintf(stderr,"Error: Can't get image (using shared memory)!\n");
        }
    } else {
        if(m_x11_image != NULL) {
            XDestroyImage(m_x11_image);
            m_x11_image = NULL;
        }
        m_x11_image = XGetImage(m_x11_display, m_x11_root, grab_x, grab_y, m_width, m_height, AllPlanes, ZPixmap);
        if(m_x11_image == NULL) {
            fprintf(stderr,"Error: Can't get image (not using shared memory)!\n");
        }
    }

    // draw the cursor
    if(m_record_cursor) {
        X11ImageDrawCursor(m_x11_display, m_x11_image, grab_x, grab_y);
    }
    // push the frame
    uint8_t *image_data = (uint8_t*) m_x11_image->data;
    m_image_stride = m_x11_image->bytes_per_line;
    AVPixelFormat x11_image_format = X11ImageGetPixelFormat(m_x11_image);

    ReadVideoFrame(m_width, m_height, image_data, m_image_stride, x11_image_format, frame, out_size, out_pixel_format);
}

void xgrab::ReadVideoFrame(unsigned int width, unsigned int height, const uint8_t* data, int stride,
                           AVPixelFormat format, AVFrame *frame, QSize out_size, AVPixelFormat out_format) {

    m_sws_context = sws_getCachedContext(m_sws_context, width, height, format,
                                         out_size.width(), out_size.height(), out_format,
                                         SWS_BILINEAR, NULL, NULL, NULL);
    if(m_sws_context == NULL) {
        Logger::LogError("[FastScaler::Scale] " + Logger::tr("Error: Can't get swscale context!", "Don't translate 'swscale'"));
        throw LibavException();
    }
    sws_setColorspaceDetails(m_sws_context,
                             sws_getCoefficients(SWS_CS_DEFAULT), 0, //TODO// need to change this for actual YUV inputs (e.g. webcam)
                             sws_getCoefficients(SWS_CS_ITU709), 0,
                             0, 1 << 16, 1 << 16);

    int slice_height = sws_scale(m_sws_context, &data, &stride, 0, height, frame->data, frame->linesize);

}
