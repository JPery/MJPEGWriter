#include "MJPEGWriter.h"

using namespace cv;

MJPEG::MJPEG(int port) : sock(INVALID_SOCKET), timeout(200000), quality(20), port(port)
{
#ifndef WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    FD_ZERO(&master);
#ifdef WIN32
    WSADATA WSAData;
    WSAStartup(MAKEWORD(2, 0), &WSAData);
#endif
}

MJPEG::~MJPEG()
{
    release();
    delete thread_listen;
    delete thread_write;
#ifdef WIN32
    WSACleanup();
#endif
}

bool MJPEG::release()
{
    if (sock != INVALID_SOCKET)
    {
        shutdown(sock, 2);
    }
    sock = (INVALID_SOCKET);
    return false;
}


bool MJPEG::open()
{
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    SOCKADDR_IN address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (bind(sock, (SOCKADDR *) &address, sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
    {
        std::cerr << "error : couldn't bind sock " << sock << " to port " << port << "!" << std::endl;
        return release();
    }
    if (listen(sock, num_connections) == SOCKET_ERROR)
    {
        std::cerr << "error : couldn't listen on sock " << sock << " on port " << port << " !" << std::endl;
        return release();
    }
    FD_SET(sock, &master);
    this->startError = false;
    return true;
}

bool MJPEG::isOpened()
{
    return sock != INVALID_SOCKET;
}

void MJPEG::start()
{
    mutex_writer.lock();
    thread_listen = new std::thread(this->listen_Helper, this);
    thread_write = new std::thread(this->writer_Helper, this);
}

void MJPEG::stop()
{
    this->release();
    thread_listen->join();
    thread_write->join();
}

void MJPEG::write(cv::Mat frame)
{
    mutex_writer.lock();
    if (!frame.empty())
    {
        lastFrame.release();
        lastFrame = frame.clone();
    }
    mutex_writer.unlock();
}

void MJPEG::Listener()
{
    // send http header
    std::string header;
    header += "HTTP/1.0 200 OK\r\n";
    header += "Cache-Control: no-cache\r\n";
    header += "Pragma: no-cache\r\n";
    header += "Connection: close\r\n";
    header += "Content-Type: multipart/x-mixed-replace; boundary=mjpegstream\r\n\r\n";
    const int header_size = header.size();
    char *header_data = (char *) header.data();
    fd_set rread;
    SOCKET maxfd;
    this->open();
    mutex_writer.unlock();
    while (true)
    {
        rread = master;
        struct timeval to = {0, timeout};
        maxfd = sock + 1;
        if (sock == INVALID_SOCKET)
        {
            return;
        }
        int sel = select(maxfd, &rread, NULL, NULL, &to);
        if (sel > 0)
        {
            for (int s = 0; s < maxfd; s++)
            {
                if (FD_ISSET(s, &rread) && s == sock)
                {
                    int addrlen = sizeof(SOCKADDR);
                    SOCKADDR_IN address = {0};
                    SOCKET client = accept(sock, (SOCKADDR *) &address, (socklen_t *) &addrlen);
                    if (client == SOCKET_ERROR)
                    {
                        std::cerr << "error : couldn't accept connection on sock " << sock << " !" << std::endl;
                        return;
                    }
                    maxfd = (maxfd > client ? maxfd : client);
                    mutex_cout.lock();
                    std::cout << "new client " << client << std::endl;
                    char headers[4096] = "\0";
                    int readBytes = _read(client, headers);
                    std::cout << headers;
                    mutex_cout.unlock();
                    mutex_client.lock();
                    _write(client, header_data, header_size);
                    clients.push_back(client);
                    mutex_client.unlock();
                }
            }
        }
        mySleep(1);
    }
}

void MJPEG::Writer()
{
    mutex_writer.lock();
    mutex_writer.unlock();
    const int milis2wait = 17;
    while (this->isOpened())
    {
        mutex_client.lock();
        int num_connected_clients = clients.size();
        mutex_client.unlock();
        if (!num_connected_clients)
        {
            mySleep(milis2wait);
            continue;
        }
        std::array<std::thread, num_connections> threads;
        int count = 0;

        std::vector<uchar> outbuf;
        std::vector<int> params;
        params.push_back(IMWRITE_JPEG_QUALITY);
        params.push_back(quality);
        mutex_writer.lock();
        imencode(".jpg", lastFrame, outbuf, params);
        mutex_writer.unlock();
        int outlen = outbuf.size();

        mutex_client.lock();
        std::vector<int>::iterator begin = clients.begin();
        std::vector<int>::iterator end = clients.end();
        mutex_client.unlock();
        std::vector<clientPayload *> payloads;
        for (std::vector<int>::iterator it = begin; it != end; ++it, ++count)
        {
            if (count > num_connections)
            {
                break;
            }
            struct clientPayload *cp = new clientPayload({(MJPEG *) this, {outbuf.data(), outlen, *it}});
            payloads.push_back(cp);
            threads[count] = std::thread(MJPEG::clientWrite_Helper, cp);
        }
        for (; count > 0; count--)
        {
            threads[count - 1].join();
            delete payloads.at(count - 1);
        }
        mySleep(milis2wait);
    }
}

void MJPEG::ClientWrite(clientFrame &cf)
{
    std::stringstream head;
    head << "--mjpegstream\r\nContent-Type: image/jpeg\r\nContent-Length: " << cf.outlen << "\r\n\r\n";
    std::string string_head = head.str();
    mutex_client.lock();
    _write(cf.client, (char *) string_head.c_str(), string_head.size());
    int n = _write(cf.client, (char *) (cf.outbuf), cf.outlen);
    if (n < cf.outlen)
    {
        std::vector<int>::iterator it;
        it = find(clients.begin(), clients.end(), cf.client);
        if (it != clients.end())
        {
            std::cerr << "kill client " << cf.client << std::endl;
            clients.erase(std::remove(clients.begin(), clients.end(), cf.client));
            ::shutdown(cf.client, 2);
        }
    }
    mutex_client.unlock();
}

int MJPEG::_write(int sock, char *s, int len)
{
    if (len < 1)
    {
        len = strlen(s);
    }
    {
        try
        {
            int retval = ::send(sock, s, len, 0);
            return retval;
        }
        catch (int e)
        {
            std::cout << "An exception occurred. Exception Nr. " << e << '\n';
        }
    }
    return -1;
}

int MJPEG::_read(int socket, char *buffer)
{
    int result;
    result = recv(socket, buffer, 4096, MSG_PEEK);
    if (result < 0)
    {
        std::cout << "An exception occurred. Exception Nr. " << result << '\n';
        return result;
    }
    std::string s = buffer;
    buffer = (char *) s.substr(0, (int) result).c_str();
    return result;
}

void MJPEG::listen_Helper(void *context)
{
    ((MJPEG *) context)->Listener();
}

void MJPEG::writer_Helper(void *context)
{
    ((MJPEG *) context)->Writer();
}

void *MJPEG::clientWrite_Helper(void *payload)
{
    void *ctx = ((clientPayload *) payload)->context;
    struct clientFrame cf = ((clientPayload *) payload)->cf;
    ((MJPEG *) ctx)->ClientWrite(cf);
    return NULL;
}

void mySleep(int sleepMs)
{
#ifdef WIN32
    Sleep(sleepMs);
#else
    usleep(sleepMs * 1000);   // usleep takes sleep time in us (1 millionth of a second)
#endif
}