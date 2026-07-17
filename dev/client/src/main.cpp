#include "client_session.h"

int main()
{
    client_session client;
    string host;
    string port;

    cout << "server ip (default 127.0.0.1): ";
    getline(cin, host);

    cout << "server port (default 9000): ";
    getline(cin, port);

    client.configure_endpoint(host, port);

    if (!client.init())
    {
        return 1;
    }

    if (!client.connect_server())
    {
        return 1;
    }

    string nickname;
    cout << "nickname: ";
    getline(cin, nickname);

    if (!client.send_nickname(nickname))
    {
        client.close();
        return 1;
    }

    if (!client.wait_for_nickname_response())
    {
        client.close();
        return 1;
    }

    thread recv_thread(&client_session::recv_loop, &client);

    while (client.is_running())
    {
        string input;
        if (!getline(cin, input))
        {
            break;
        }

        if (input == "exit")
        {
            break;
        }

        if (!input.empty() && !client.send_chat(input))
        {
            break;
        }
    }

    client.close();

    if (recv_thread.joinable())
    {
        recv_thread.join();
    }

    return 0;
}
