#-------------------------------------------------
#
# Project created by QtCreator 2016-09-06T16:07:48
#
#-------------------------------------------------

QT       += core gui widgets x11extras

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = grab_test
TEMPLATE = app


SOURCES += main.cpp\
        maintest.cpp \
    xgrab.cpp \
    Logger.cpp \
    AV/Output/BaseEncoder.cpp \
    AV/Output/Muxer.cpp \
    AV/Output/VideoEncoder.cpp

HEADERS  += maintest.h \
    xgrab.h \
    global.h \
    Logger.h \
    TempBuffer.h \
    AV/Output/BaseEncoder.h \
    AV/Output/Muxer.h \
    AV/Output/VideoEncoder.h \
    AV/Output/OutputSettings.h

FORMS    += maintest.ui

DEFINES += SSR_USE_X86_ASM=1 SSR_USE_FFMPEG_VERSIONS=1
LIBS += -lX11 -lXext -lXfixes -lasound -lavformat -lavcodec -lavutil -lswscale
LIBS += -L/home/domon/ffmpeg_build/lib
INCLUDEPATH += /home/domon/ffmpeg_build/include AV AV/Input AV/Output
