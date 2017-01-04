#ifndef MAINTEST_H
#define MAINTEST_H

#include "global.h"
#include "Muxer.h"
#include "VideoEncoder.h"
#include "OutputSettings.h"

namespace Ui {
class MainTest;
}

class xgrab;

///////////////////////////////////////////////
class MainTest : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainTest(QWidget *parent = 0);
    ~MainTest();
    enum enum_video_area {
        VIDEO_AREA_SCREEN,
        VIDEO_AREA_FIXED,
        VIDEO_AREA_CURSOR,
        VIDEO_AREA_GLINJECT,
        VIDEO_AREA_COUNT // must be last
    };

protected:
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void paintEvent(QPaintEvent* event) override;

signals:
    void NeedsUpdate();

private:
    void StartGrabbing();
    void StopGrabbing();
    void SetVideoAreaFromRubberBand();
    void GrabThread();
    void PushVideoFrame();
    bool m_grabbing, m_selecting_window, m_should_stop;
    std::thread m_thread;
    std::unique_ptr<QRubberBand> m_rubber_band, m_recording_frame;
    QRect m_ret,m_rubber_band_rect, m_select_window_outer_rect, m_select_window_inner_rect;
    uint32_t m_video_frame_rate;

    //muxer & encoders
    OutputSettings m_output_settings;
    std::unique_ptr<Muxer> m_muxer;
    VideoEncoder *m_video_encoder;

private slots:

    void on_m_grab_rect_clicked();
    void on_m_grab_window_clicked();
    void on_start_clicked();

    void on_stop_clicked();

private:
    Ui::MainTest *ui;
    VideoEncoder *video_encoder;
    std::unique_ptr<xgrab> x11;

private:
    QWidget *centralWidget;
    QButtonGroup *m_buttongroup_video_area;
    QPushButton *m_pushbutton_video_select_rectangle, *m_pushbutton_video_select_window, *m_pushbutton_video_opengl_settings;
    QLabel *m_label_video_x, *m_label_video_y, *m_label_video_w, *m_label_video_h;
    QSpinBox *m_spinbox_video_frame_rate;
    QCheckBox *m_checkbox_scale;
    QLabel *m_label_video_scaled_w, *m_label_video_scaled_h;
    QSpinBox *m_spinbox_video_scaled_w, *m_spinbox_video_scaled_h;
    QCheckBox *m_checkbox_record_cursor;

    QCheckBox *m_checkbox_audio_enable;
    QLabel *m_label_audio_backend;
    QComboBox *m_combobox_audio_backend;
    QLabel *m_label_alsa_source;
    QComboBox *m_combobox_alsa_source;
    QPushButton *m_pushbutton_alsa_refresh;
};

#endif // MAINTEST_H
