#ifndef XGRAB_H
#define XGRAB_H

#include "global.h"
#include "TempBuffer.h"

class xgrab
{
public:
    xgrab(unsigned int x, unsigned int y, unsigned int width, unsigned int height, bool record_cursor, bool follow_cursor);
    ~xgrab();
    void GetQimage(AVFrame *frame, AVPixelFormat out_pixel_format, QSize out_size);
    uint8_t * Forpreview();
    void ReadVideoFrame(unsigned int width, unsigned int height, const uint8_t* data, int stride,
                        AVPixelFormat format, AVFrame *frame, QSize out_size, AVPixelFormat out_format);

public:
    std::shared_ptr<TempBuffer<uint8_t> > m_image_buffer;
    int m_image_stride;
    QSize m_image_size;

private:
    unsigned int m_x, m_y, m_width, m_height;
    bool m_record_cursor, m_follow_cursor;

    Display *m_x11_display;
    int m_x11_screen;
    Window m_x11_root;
    Visual *m_x11_visual;
    int m_x11_depth;
    bool m_x11_use_shm;
    XShmSegmentInfo m_x11_shm_info;
    bool m_x11_shm_server_attached;
    XImage *m_x11_image;
    SwsContext *m_sws_context;

private:
    void Init();
    void Free();
};

QSize CalculateScaledSize(QSize in, QSize out);

#endif // XGRAB_H
