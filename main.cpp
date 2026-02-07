#include "mainwindow.h"
#include <QApplication>
#include <QMessageBox>
#include "db.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    if (!Db::init())
    {
        QMessageBox::critical(nullptr, "DB Error", "数据库初始化失败，请看控制台输出");
        return 1;
    }

    MainWindow w;
    w.show();
    return a.exec();
}
