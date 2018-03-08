#include "MJPEGWriter.h"
#include <fstream>
void
MJPEGWriter::Listener()
{
    fd_set rread;
    SOCKET maxfd;
    while (true)
    {
        rread = master;
        struct timeval to = { 0, timeout };
        maxfd = sock + 1;

        int sel = select(maxfd, &rread, NULL, NULL, &to);
        if (sel > 0) {
            for (int s = 0; s < maxfd; s++)
            {
                if (FD_ISSET(s, &rread) && s == sock)
                {
                    int         addrlen = sizeof(SOCKADDR);
                    SOCKADDR_IN address = { 0 };
                    SOCKET      client = accept(sock, (SOCKADDR*)&address, (socklen_t*)&addrlen);
                    if (client == SOCKET_ERROR)
                    {
                        cerr << "error : couldn't accept connection on sock " << sock << " !" << endl;
                        return;
                    }
                    maxfd = (maxfd>client ? maxfd : client);
                    pthread_mutex_lock(&mutex_cout);
                    cout << "new client " << client << endl;
                    pthread_mutex_unlock(&mutex_cout);
                    pthread_mutex_lock(&mutex_client);
                    _write(client, (char*)"HTTP/1.0 200 OK\r\n", 0);
                    _write(client, (char*)
                        "Server: Mozarella/2.2\r\n"
                        "Accept-Range: bytes\r\n"
                        "Connection: close\r\n"
                        "Max-Age: 0\r\n"
                        "Expires: 0\r\n"
                        "Cache-Control: no-cache, private\r\n"
                        "Pragma: no-cache\r\n"
                        "Content-Type: multipart/x-mixed-replace; boundary=mjpegstream\r\n"
                        "\r\n", 0);
                    clients.push_back(client);
                    pthread_mutex_unlock(&mutex_client);
                }
            }
        }
        usleep(1000);
    }
}

void
MJPEGWriter::Writer()
{
    while (this->isOpened())
    {
        pthread_t threads[NUM_CONNECTIONS];
        int count = 0;

        std::vector<uchar> outbuf;
        std::vector<int> params;
        params.push_back(CV_IMWRITE_JPEG_QUALITY);
        params.push_back(quality);
        pthread_mutex_lock(&mutex_writer);
        imencode(".jpg", lastFrame, outbuf, params);
        pthread_mutex_unlock(&mutex_writer);
        int outlen = outbuf.size();

        pthread_mutex_lock(&mutex_client);
        std::vector<int>::iterator begin = clients.begin();
        std::vector<int>::iterator end = clients.end();
        pthread_mutex_unlock(&mutex_client);
        std::vector<clientPayload*> payloads;
        for (std::vector<int>::iterator it = begin; it != end; ++it, ++count)
        {
            if (count > NUM_CONNECTIONS)
                break;
            struct clientPayload *cp = new clientPayload({ (MJPEGWriter*)this, { outbuf.data(), outlen, *it } });
            payloads.push_back(cp);
            pthread_create(&threads[count], NULL, &MJPEGWriter::clientWrite_Helper, cp);
        }
        for (; count > 0; count--)
        {
            pthread_join(threads[count-1], NULL);
            delete payloads.at(count-1);
        }
        usleep(16666);
    }
}

void
MJPEGWriter::ClientWrite(clientFrame & cf)
{
    stringstream head;
    head << "--mjpegstream\r\nContent-Type: image/jpeg\r\nContent-Length: " << cf.outlen << "\r\n\r\n";
    char* c_head = (char*) head.str().c_str();
    pthread_mutex_lock(&mutex_client);
    _write(cf.client, c_head, 0);
    int n = _write(cf.client, (char*)(cf.outbuf), cf.outlen);
	if (n < cf.outlen)
	{
    	std::vector<int>::iterator it;
      	it = find (clients.begin(), clients.end(), cf.client);
      	if (it != clients.end())
      	{
      		cerr << "kill client " << cf.client << endl;
      		clients.erase(std::remove(clients.begin(), clients.end(), cf.client));
            	::shutdown(cf.client, 2);
      	}
	}
    pthread_mutex_unlock(&mutex_client);
    pthread_exit(NULL);
}
