#include "MJPEGWriter.h"
#include "base64.h"
#include <fstream>

const std::string ok_header = "HTTP/1.0 200 OK\r\nCache-Control: no-cache\r\nPragma: no-cache\r\nConnection: close\r\nContent-Type: multipart/x-mixed-replace; boundary=mjpegstream\r\n\r\n";
const std::string unauthorized_header = "HTTP/1.0 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"MJPEG stream\"\r\n\r\n";
const std::string not_allowed_header = "HTTP/1.0 405 Method Not Allowed\r\nContent-Type: text/html\r\nAllow: GET\r\n<h1>Method not allowed</h1>\r\n\r\n";


std::map<std::string, std::string>
parseHeaders(std::string headers){
	std::map<std::string, std::string> headers_map;
	std::string delimiter = "\r\n";
	size_t pos = 0;
	bool first = true;
	while ((pos = headers.find(delimiter)) != std::string::npos) {
		if(first){
			first = false;
			std::string first_header = headers.substr(0, pos);
			headers.erase(0, pos + delimiter.length());

        	size_t del_pos = first_header.find(" ");
			std::string method = first_header.substr(0, del_pos);
			headers_map["Method"] = method;
			first_header.erase(0, del_pos + 1);

			del_pos = first_header.find(" ");
			std::string path = first_header.substr(0, del_pos);
			headers_map["Path"] = path;
			first_header.erase(0, del_pos + 1);
			std::string http_version = first_header;
			headers_map["HTTP-Version"] = http_version;
			continue;
		}
		std::string header = headers.substr(0, pos);
		headers.erase(0, pos + delimiter.length());
		size_t header_del_pos = header.find(": ");
		std::string key = header.substr(0, header_del_pos);
		header.erase(0, header_del_pos + 2);
		std::string value = header;
		headers_map[key] = value;
	}
	return headers_map;
}

void
MJPEGWriter::Listener()
{
    fd_set rread;
    SOCKET maxfd;
    if(!this->open()){
    	pthread_mutex_unlock(&mutex_writer);
    	return;
    }
    pthread_mutex_unlock(&mutex_writer);
    while (true)
    {
        rread = master;
        struct timeval to = { 0, timeout };
        maxfd = sock + 1;
        if (sock == INVALID_SOCKET){
        	return;
        }
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
                    	std::cerr << "error : couldn't accept connection on sock " << sock << " !" << std::endl;
                        return;
                    }
                    maxfd = (maxfd>client ? maxfd : client);
                    pthread_mutex_lock(&mutex_cout);
                    std::cout << "new client " << client << std::endl;
                    pthread_mutex_unlock(&mutex_cout);
                    char headers[4096] = "\0";
                    pthread_mutex_lock(&mutex_client);
                    int read_bytes = _read(client, headers);
                    pthread_mutex_unlock(&mutex_client);
                    if (read_bytes == 0){
                        pthread_mutex_lock(&mutex_client);
                        //_write(client, (char*)not_allowed_header.data(), not_allowed_header.size());
                        ::shutdown(client, 2);
						pthread_mutex_unlock(&mutex_client);
                        pthread_mutex_lock(&mutex_cout);
                        std::cerr << "kill client " << client << " (No HTTP Headers)" << std::endl;
                        pthread_mutex_unlock(&mutex_cout);
                        continue;
                    }
                    std::map<std::string, std::string> headers_map = parseHeaders(headers);
                    if(headers_map["Method"] != "GET"){
						pthread_mutex_lock(&mutex_client);
						_write(client, (char*)not_allowed_header.data(), not_allowed_header.size());
						::shutdown(client, 2);
						pthread_mutex_unlock(&mutex_client);
						pthread_mutex_lock(&mutex_cout);
						std::cerr << "kill client " << client << " (HTTP 405)" << std::endl;
						pthread_mutex_unlock(&mutex_cout);
                        continue;
                    }
                    if (this->require_auth){
						if(headers_map.count("Authorization") > 0){
							std::string auth_header = headers_map["Authorization"];

							size_t del_pos = auth_header.find(" ");
							std::string auth_type = auth_header.substr(0, del_pos);
							auth_header.erase(0, del_pos + 1);
							std::string auth_token = auth_header;
							std::string user_pass_str = base64_decode(auth_token);

							del_pos = user_pass_str.find(":");
							std::string user = user_pass_str.substr(0, del_pos);
							user_pass_str.erase(0, del_pos + 1);
							std::string pass = user_pass_str;

							if(auth_type == "Basic" && authorized_users.count(user) > 0 && authorized_users[user] == pass){
								pthread_mutex_lock(&mutex_client);
								_write(client, (char*)ok_header.data(), ok_header.size());
								clients.push_back(client);
								pthread_mutex_unlock(&mutex_client);
							}
							else{
								pthread_mutex_lock(&mutex_client);
								_write(client, (char*)unauthorized_header.data(), unauthorized_header.size());
								::shutdown(client, 2);
								pthread_mutex_unlock(&mutex_client);
								pthread_mutex_lock(&mutex_cout);
								std::cerr << "kill client " << client << " (HTTP 401)" << std::endl;
								pthread_mutex_unlock(&mutex_cout);
							}
						}
						else{
							pthread_mutex_lock(&mutex_client);
							_write(client, (char*)unauthorized_header.data(), unauthorized_header.size());
							::shutdown(client, 2);
							pthread_mutex_unlock(&mutex_client);
							pthread_mutex_lock(&mutex_cout);
							std::cerr << "kill client " << client << " (HTTP 401)" << std::endl;
							pthread_mutex_unlock(&mutex_cout);
						}
                    }
                    else{
						pthread_mutex_lock(&mutex_client);
						_write(client, (char*)ok_header.data(), ok_header.size());
						clients.push_back(client);
						pthread_mutex_unlock(&mutex_client);
                    }
                }
            }
        }
        usleep(1000);
    }
}

void
MJPEGWriter::Writer()
{
    pthread_mutex_lock(&mutex_writer);
    pthread_mutex_unlock(&mutex_writer);
    const int milis2wait = 16666;
    while (this->isOpened())
    {
        pthread_mutex_lock(&mutex_client);
        int num_connected_clients = clients.size();
        pthread_mutex_unlock(&mutex_client);
        if (!num_connected_clients) {
            usleep(milis2wait);
            continue;
        }
        pthread_t threads[NUM_CONNECTIONS];
        int count = 0;

        std::vector<uchar> outbuf;
        std::vector<int> params;
        params.push_back(cv::IMWRITE_JPEG_QUALITY);
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
        usleep(milis2wait);
    }
}

void
MJPEGWriter::ClientWrite(clientFrame & cf)
{
	std::stringstream head;
    head << "--mjpegstream\r\nContent-Type: image/jpeg\r\nContent-Length: " << cf.outlen << "\r\n\r\n";
    std::string string_head = head.str();
    pthread_mutex_lock(&mutex_client);
    _write(cf.client, (char*) string_head.c_str(), string_head.size());
    int n = _write(cf.client, (char*)(cf.outbuf), cf.outlen);
	if (n < cf.outlen)
	{
    	std::vector<int>::iterator it;
      	it = find (clients.begin(), clients.end(), cf.client);
      	if (it != clients.end())
      	{
      		std::cerr << "kill client " << cf.client << " (Disconnected)" << std::endl;
      		clients.erase(std::remove(clients.begin(), clients.end(), cf.client));
            	::shutdown(cf.client, 2);
      	}
	}
    pthread_mutex_unlock(&mutex_client);
    pthread_exit(NULL);
}
