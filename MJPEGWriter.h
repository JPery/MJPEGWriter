#ifndef MJPEG_MJPEG_H
#define MJPEG_MJPEG_H

#ifdef WIN32
#include <Ws2tcpip.h>
#include <winsock2.h>
#include <Windows.h>
#else
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <csignal>

#define SOCKET    int
#define HOSTENT  struct hostent
#define SOCKADDR    struct sockaddr
#define SOCKADDR_IN  struct sockaddr_in
#define INVALID_SOCKET -1
#define SOCKET_ERROR   -1
#endif

#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <thread>
#include <mutex>
#include <array>
#include <opencv2/opencv.hpp>


struct clientFrame
{
    unsigned char *outbuf;
    int outlen;
    int client;
};

struct clientPayload
{
    void *context;
    clientFrame cf;
};

void mySleep(int sleepMs);

class MJPEG
{
public:
    MJPEG(int port = 0);

    ~MJPEG();

    bool release();

    bool open();

    bool isOpened();

    void start();

    void stop();

    void write(cv::Mat frame);

    bool startError = true;

private:
    SOCKET sock;
    fd_set master;
    int timeout;
    int quality; // jpeg compression [1..100]
    std::vector<int> clients;
    std::thread *thread_listen, *thread_write;
    std::mutex mutex_writer, mutex_cout, mutex_client;
    cv::Mat lastFrame;
    int port;
    const static unsigned short num_connections = 10;

    int _write(int sock, char *s, int len);

    int _read(int socket, char *buffer);

    static void listen_Helper(void *context);

    static void writer_Helper(void *context);

    static void *clientWrite_Helper(void *payload);

    void Listener();

    void Writer();

    void ClientWrite(clientFrame &cf);
};


#endif //MJPEG_MJPEG_H
