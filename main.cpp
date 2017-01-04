#include "maintest.h"
#include <QApplication>
#include "Logger.h"
int main(int argc, char *argv[])
{
    Logger logger;
    av_register_all();
    avcodec_register_all();
    avformat_network_init();
    Q_UNUSED(logger);
    QApplication a(argc, argv);
    MainTest w;
    w.show();

    return a.exec();
}
