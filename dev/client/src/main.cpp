#include <QApplication>

#include "chat_window.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    chat_window window;
    window.show();

    return app.exec();
}
