#include "Editor.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setOrganizationDomain("iskrembilen.com");
    a.setApplicationName("8bit-programmer");
    Editor w;
    w.show();
    return a.exec();
}
