#include "maintest.h"
#include "ui_maintest.h"
#include "global.h"
#include "xgrab.h"
#include <QDebug>

MainTest::MainTest(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainTest)
{
    ui->setupUi(this);
    m_should_stop = true;
    m_video_frame_rate = 25;
    m_output_settings.container_avname="matroska";
    m_output_settings.video_codec_avname="libx264";
    m_output_settings.file="/tmp/test.mkv";
    m_output_settings.video_frame_rate=25;
    m_output_settings.video_kbit_rate=5000;
}

MainTest::~MainTest()
{
    delete ui;
    if(m_thread.joinable()){
        m_should_stop=true;
        m_thread.join();
    }
}

// Tries to find the real window that corresponds to a top-level window (the actual window without window manager decorations).
// Returns None if it can't find the window (probably because the window is not handled by the window manager).
// Based on the xprop source code (http://cgit.freedesktop.org/xorg/app/xprop/tree/clientwin.c).
static Window X11FindRealWindow(Display* display, Window window) {

    // is this the real window?
    Atom actual_type;
    int actual_format;
    unsigned long items, bytes_left;
    unsigned char *data = NULL;
    XGetWindowProperty(display, window, XInternAtom(display, "WM_STATE", true),
                       0, 0, false, AnyPropertyType, &actual_type, &actual_format, &items, &bytes_left, &data);
    if(data != NULL)
        XFree(data);
    if(actual_type != None)
        return window;

    // get the child windows
    Window root, parent, *childs;
    unsigned int childcount;
    if(!XQueryTree(display, window, &root, &parent, &childs, &childcount)) {
        return None;
    }

    // recursively call this function for all childs
    Window real_window = None;
    for(unsigned int i = childcount; i > 0; ) {
        --i;
        Window w = X11FindRealWindow(display, childs[i]);
        if(w != None) {
            real_window = w;
            break;
        }
    }

    // free child window list
    if(childs != NULL)
        XFree(childs);

    return real_window;

}

// This does some sanity checking on the rubber band rectangle before creating it.
// Rubber bands with width or height zero or extremely large appear to cause problems.
static QRect ValidateRubberBandRectangle(QRect rect) {
    QRect screenrect = QApplication::desktop()->screenGeometry(0);
    for(int i = 1; i < QApplication::desktop()->screenCount(); ++i) {
        screenrect |= QApplication::desktop()->screenGeometry(i);
    }
    rect = rect.normalized();
    rect &= screenrect.adjusted(-10, -10, 10, 10);
    return (rect.isNull())? QRect(-10, -10, 1, 1) : rect;
}

static AVFrame* CreateVideoFrame(unsigned int width, unsigned int height, AVPixelFormat pixel_format) {

    // get required planes
    unsigned int planes = 0;
    size_t linesize[3] = {0}, planesize[3] = {0};
    switch(pixel_format) {
        case AV_PIX_FMT_YUV444P: {
            // Y/U/V = 1 byte per pixel
            planes = 3;
            linesize[0]  = grow_align16(width); planesize[0] = linesize[0] * height;
            linesize[1]  = grow_align16(width); planesize[1] = linesize[1] * height;
            linesize[2]  = grow_align16(width); planesize[2] = linesize[2] * height;
            break;
        }
        case AV_PIX_FMT_YUV422P: {
            // Y = 1 byte per pixel, U/V = 1 byte per 2x1 pixels
            assert(width % 2 == 0);
            planes = 3;
            linesize[0]  = grow_align16(width    ); planesize[0] = linesize[0] * height;
            linesize[1]  = grow_align16(width / 2); planesize[1] = linesize[1] * height;
            linesize[2]  = grow_align16(width / 2); planesize[2] = linesize[2] * height;
            break;
        }
        case AV_PIX_FMT_YUV420P: {
            // Y = 1 byte per pixel, U/V = 1 byte per 2x2 pixels
            assert(width % 2 == 0);
            assert(height % 2 == 0);
            planes = 3;
            linesize[0]  = grow_align16(width    ); planesize[0] = linesize[0] * height    ;
            linesize[1]  = grow_align16(width / 2); planesize[1] = linesize[1] * height / 2;
            linesize[2]  = grow_align16(width / 2); planesize[2] = linesize[2] * height / 2;
            break;
        }
        case AV_PIX_FMT_NV12: {
            assert(width % 2 == 0);
            assert(height % 2 == 0);
            // planar YUV 4:2:0, 12bpp, 1 plane for Y and 1 plane for the UV components, which are interleaved
            // Y = 1 byte per pixel, U/V = 1 byte per 2x2 pixels
            planes = 2;
            linesize[0]  = grow_align16(width); planesize[0] = linesize[0] * height    ;
            linesize[1]  = grow_align16(width); planesize[1] = linesize[1] * height / 2;
            break;
        }
        case AV_PIX_FMT_BGRA: {
            // BGRA = 4 bytes per pixel
            planes = 1;
            linesize[0] = grow_align16(width * 4); planesize[0] = linesize[0] * height;
            break;
        }
        case AV_PIX_FMT_BGR24: {
            // BGR = 3 bytes per pixel
            planes = 1;
            linesize[0] = grow_align16(width * 3); planesize[0] = linesize[0] * height;
            break;
        }
        default: assert(false); break;
    }

    // create the frame
    size_t totalsize = 0;
    for(unsigned int p = 0; p < planes; ++p) {
        totalsize += planesize[p];
    }
    //alloc frame
    AVFrame *frame;
#if SSR_USE_AV_FRAME_ALLOC
    frame = av_frame_alloc();
#else
    frame = avcodec_alloc_frame();
#endif
    if(frame == NULL)
        std::bad_alloc();
#if SSR_USE_AVFRAME_EXTENDED_DATA
    // ffmpeg docs say that extended_data should point to data if it isn't used
    frame->extended_data = frame->data;
#endif
    uint8_t *data = (uint8_t*) av_malloc(totalsize);
    if(data == NULL)
        throw std::bad_alloc();

    for(unsigned int p = 0; p < planes; ++p) {
        frame->data[p] = data;
        frame->linesize[p] = linesize[p];
        data += planesize[p];
    }
#if SSR_USE_AVFRAME_WIDTH_HEIGHT
    frame->width = width;
    frame->height = height;
#endif
#if SSR_USE_AVFRAME_FORMAT
    frame->format = pixel_format;
#endif
#if SSR_USE_AVFRAME_SAR
    frame->sample_aspect_ratio.num = 1;
    frame->sample_aspect_ratio.den = 1;
#endif

    return frame;

}

static void Free_avframe(AVFrame *frame){
    if(frame != NULL) {
#if SSR_USE_AV_FRAME_FREE
        av_frame_free(&frame);
#elif SSR_USE_AVCODEC_FREE_FRAME
        avcodec_free_frame(&frame);
#else
        av_free(frame);
#endif
    }
}

void MainTest::SetVideoAreaFromRubberBand() {
    m_ret = m_rubber_band_rect.normalized();
}

void MainTest::StopGrabbing() {
    m_rubber_band.reset();
    setMouseTracking(false);
    releaseKeyboard();
    releaseMouse();
    this->raise();
    this->activateWindow();
    if(m_selecting_window)
        ui->m_grab_window->setDown(false);
    else
        ui->m_grab_rect->setDown(false);
    m_grabbing = false;
}

void MainTest::mousePressEvent(QMouseEvent* event) {
    if(m_grabbing) {
        if(event->button() == Qt::LeftButton) {
            if(m_selecting_window) {
                // As expected, Qt does not provide any functions to find the window at a specific position, so I have to use Xlib directly.
                // I'm not completely sure whether this is the best way to do this, but it appears to work. XQueryPointer returns the window
                // currently below the mouse along with the mouse position, but apparently this may not work correctly when the mouse pointer
                // is also grabbed (even though it works fine in my test), so I use XTranslateCoordinates instead. Originally I wanted to
                // show the rubber band when the mouse hovers over a window (instead of having to click it), but this doesn't work correctly
                // since X will simply return a handle the rubber band itself (even though it should be transparent to mouse events).
                Window selected_window;
                int x, y;
                if(XTranslateCoordinates(QX11Info::display(), QX11Info::appRootWindow(), QX11Info::appRootWindow(), event->globalX(), event->globalY(), &x, &y, &selected_window)) {
                    XWindowAttributes attributes;
                    if(selected_window != None && XGetWindowAttributes(QX11Info::display(), selected_window, &attributes)) {

                        // naive outer/inner rectangle, this won't work for window decorations
                        m_select_window_outer_rect = QRect(attributes.x, attributes.y, attributes.width + 2 * attributes.border_width, attributes.height + 2 * attributes.border_width);
                        m_select_window_inner_rect = QRect(attributes.x + attributes.border_width, attributes.y + attributes.border_width, attributes.width, attributes.height);

                        // try to find the real window (rather than the decorations added by the window manager)
                        Window real_window = X11FindRealWindow(QX11Info::display(), selected_window);
                        if(real_window != None) {
                            Atom actual_type;
                            int actual_format;
                            unsigned long items, bytes_left;
                            long *data = NULL;
                            int result = XGetWindowProperty(QX11Info::display(), real_window, XInternAtom(QX11Info::display(), "_NET_FRAME_EXTENTS", true),
                                                            0, 4, false, AnyPropertyType, &actual_type, &actual_format, &items, &bytes_left, (unsigned char**) &data);
                            if(result == Success) {
                                if(items == 4 && bytes_left == 0 && actual_format == 32) { // format 32 means 'long', even if long is 64-bit ...
                                    Window child;
                                    // the attributes of the real window only store the *relative* position which is not what we need, so use XTranslateCoordinates again
                                    if(XTranslateCoordinates(QX11Info::display(), real_window, QX11Info::appRootWindow(), 0, 0, &x, &y, &child)
                                             && XGetWindowAttributes(QX11Info::display(), real_window, &attributes)) {

                                        // finally!
                                        m_select_window_inner_rect = QRect(x, y, attributes.width, attributes.height);
                                        m_select_window_outer_rect = m_select_window_inner_rect.adjusted(-data[0], -data[2], data[1], data[3]);

                                    } else {

                                        // I doubt this will ever be needed, but do it anyway
                                        m_select_window_inner_rect = m_select_window_outer_rect.adjusted(data[0], data[2], -data[1], -data[3]);

                                    }
                                }
                            }
                            if(data != NULL)
                                XFree(data);
                        }

                        // pick the inner rectangle if the users clicks inside the window, or the outer rectangle otherwise
                        m_rubber_band_rect = (m_select_window_inner_rect.contains(event->globalPos()))? m_select_window_inner_rect : m_select_window_outer_rect;
                        m_rubber_band.reset(new QRubberBand(QRubberBand::Line));
                        m_rubber_band->setWindowOpacity(0.4);
                        m_rubber_band->setGeometry(ValidateRubberBandRectangle(m_rubber_band_rect));
                        m_rubber_band->show();

                    }
                }
            } else {
                m_rubber_band_rect = QRect(event->globalPos(), QSize(0, 0));
                m_rubber_band.reset(new QRubberBand(QRubberBand::Line));
                m_rubber_band->setWindowOpacity(0.4);
                m_rubber_band->setGeometry(ValidateRubberBandRectangle(m_rubber_band_rect));
                m_rubber_band->show();
            }
        } else {
            StopGrabbing();
        }
        event->accept();
        return;
    }
    event->ignore();
}

void MainTest::mouseReleaseEvent(QMouseEvent* event) {
    if(m_grabbing) {
        if(event->button() == Qt::LeftButton) {
            if(m_rubber_band != NULL) {
                SetVideoAreaFromRubberBand();
                m_rubber_band->hide();
            }
        }
        StopGrabbing();
        event->accept();
        qDebug("the rect info is height: %d, width: %d \n", m_ret.height(),m_ret.width());
        //sleep to wait rubber disappear
        usleep(50000);
        x11.reset(new xgrab(m_ret.x(),m_ret.y(),m_ret.width()/2*2,m_ret.height()/2*2,true,false));
        QSize out_size=QSize(m_ret.width()/2*2,m_ret.height()/2*2);
        //badly preview for test
        uint8_t *previewer = x11->Forpreview();
        if(previewer != NULL) {

            // create image (data is not copied)
            QImage img((uchar *)previewer, out_size.width(), out_size.height(), x11->m_image_stride, QImage::Format_RGB32);
            img.scaled(ui->label_2->size());
            ui->label_2->setPixmap(QPixmap::fromImage(img));

        }
        m_should_stop=false;
        return;
    }
    event->ignore();
}

void MainTest::mouseMoveEvent(QMouseEvent* event) {
    if(m_grabbing) {
        if(m_rubber_band != NULL) {
            if(m_selecting_window) {
                // pick the inner rectangle if the users clicks inside the window, or the outer rectangle otherwise
                m_rubber_band_rect = (m_select_window_inner_rect.contains(event->globalPos()))? m_select_window_inner_rect : m_select_window_outer_rect;
            } else {
                m_rubber_band_rect.setBottomRight(event->globalPos());
            }
            m_rubber_band->setGeometry(ValidateRubberBandRectangle(m_rubber_band_rect));
        }
        event->accept();
        return;
    }
    event->ignore();
}

void MainTest::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
}

void MainTest::StartGrabbing(){
    m_grabbing = true;
    if(m_selecting_window)
        ui->m_grab_window->setDown(true);
    else
        ui->m_grab_rect->setDown(true);
    this->lower();
    grabMouse(Qt::CrossCursor);
    grabKeyboard();
    setMouseTracking(true);
}

void MainTest::keyPressEvent(QKeyEvent* event) {
    if(m_grabbing) {
        if(event->key() == Qt::Key_Escape) {
            StopGrabbing();
            return;
        }
        event->accept();
        return;
    }
    event->ignore();
}

void MainTest::on_m_grab_rect_clicked()
{
    m_selecting_window = false;
    StartGrabbing();
}

void MainTest::on_m_grab_window_clicked()
{
    m_selecting_window = true;
    StartGrabbing();
}

void MainTest::GrabThread() {

    int64_t last_timestamp = hrt_time_micro();
    int64_t timestamp;
    int64_t next_timestamp = last_timestamp;
    int64_t local_pts=0;
    //while(m_should_stop)sleep(1);
    std::unique_ptr<Muxer> muxer(new Muxer(m_output_settings.container_avname,m_output_settings.file));
    //VideoEncoder *video_encoder = NULL;
    AVFrame *frame=NULL;
    //int out_stride = grow_align16(m_ret.width() * 4);
    QSize out_size=QSize(m_ret.width()/2*2,m_ret.height()/2*2);

    if(!m_output_settings.video_codec_avname.isEmpty())
        video_encoder = muxer->AddVideoEncoder(m_output_settings.video_codec_avname, m_output_settings.video_options, m_output_settings.video_kbit_rate * 1024,
                                               out_size.width(), out_size.height(), m_output_settings.video_frame_rate);
    muxer->Start();
    while(!m_should_stop){
        next_timestamp = last_timestamp + (int64_t)(1000000/m_video_frame_rate);
        timestamp = hrt_time_micro();
        if(timestamp < next_timestamp){
            usleep(next_timestamp-timestamp);
            continue;
        }
        //usleep(40000);
        frame=CreateVideoFrame(out_size.width(),out_size.height(), video_encoder->GetPixelFormat());
        x11->GetQimage(frame, video_encoder->GetPixelFormat(), out_size);
        frame->pts=local_pts;//local_pts;
        local_pts++;

        //badly preview for test
        uint8_t *previewer = x11->Forpreview();
        if(previewer != NULL) {

            // create image (data is not copied)
            QImage img((uchar *)previewer, out_size.width(), out_size.height(), x11->m_image_stride, QImage::Format_RGB32);
            ui->label_2->setPixmap(QPixmap::fromImage(img));

        }

        video_encoder->EncodeFrame(frame);

        last_timestamp = timestamp;
    }

}

void MainTest::on_start_clicked()
{
    if(m_thread.joinable()){
        m_should_stop=true;
        m_thread.join();
    }
    m_should_stop=false;
    m_thread=std::thread(&MainTest::GrabThread, this);
}

void MainTest::on_stop_clicked()
{
    if(m_thread.joinable()){
        m_should_stop=true;
        m_thread.join();
    }
}
