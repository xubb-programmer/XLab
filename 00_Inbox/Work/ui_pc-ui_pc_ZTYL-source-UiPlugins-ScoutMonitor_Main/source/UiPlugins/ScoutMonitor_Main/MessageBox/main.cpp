#include "MessageBoxWidget.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MessageBoxWidget w;
    w.show();

    return a.exec();
}
