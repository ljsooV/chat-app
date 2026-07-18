#include "server_session.h"

#include <Windows.h>

int main()
{
    SetConsoleOutputCP(CP_UTF8);

    server_session server;

    if (!server.init())
    {
        return 1;
    }

    if (!server.start_server())
    {
        return 1;
    }

    server.accept_loop();

    return 0;
}
