
#pragma once

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <cstring>
#include <string>
#include <map>
#include <memory>

#include <iostream>
using std::cout;

#include "httpconnection.h"

using std::string;
using std:: map;
using std::shared_ptr;
using std::make_shared;

class HttpServer 
{
public:
    const int MIN_WAITING_EVENTS = 10;
public:
	HttpServer(const string &host_addr, uint16_t port, const string &directory) : _host_addr(host_addr), _port(port), _directory(directory), _running(false) {
		server_socket = -1;
		server_epollfd = -1;
	}
	~HttpServer() {
		if (!_running) {
			return;
		}
		if (server_socket != -1) {
			close(server_socket);
		}
		if (server_epollfd != -1) {
			close(server_epollfd);
		}
	}
	bool run() {
		if (_running) return false;
		if (0 != chdir(_directory.c_str())) {
			error_message = string(strerror(errno));
			return false;			
		}
		int epollfd = epoll_create1(0);
		if (-1 == epollfd) {
			error_message = string(strerror(errno));
			return false;
		}
		int sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
		if (-1 == sock) {
			error_message = string(strerror(errno));
			close(epollfd);
			return false;
		}
		int optval = 1;
		setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
		struct sockaddr_in server_addr;
		server_addr.sin_family = AF_INET;
		server_addr.sin_port = htons(_port);
		inet_aton(_host_addr.c_str(), &server_addr.sin_addr);
		if (-1 == bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr))) {
			error_message = string(strerror(errno));
			close(sock);
			close(epollfd);			
			return false;
		}
		if (-1 == listen(sock, SOMAXCONN)) {
			error_message = string(strerror(errno));
			close(sock);
			close(epollfd);
			return false;			
		}
		struct epoll_event listen_event; listen_event.data.fd = sock; listen_event.events = EPOLLIN;
		if (-1 == epoll_ctl(epollfd, EPOLL_CTL_ADD, sock, &listen_event)) {
			error_message = string(strerror(errno));
			close(sock);
			close(epollfd);
			return false;						
		}
		//ready!!!
		server_socket = sock;
		server_epollfd = epollfd;
		_running = true;
		return waitAndServeClients();
	}
	string getLastError() {return error_message;}
private:
	int setNonBlock(int fd) {
		int flags;
#if defined(O_NONBLOCK)
		if (-1 == (flags = fcntl(fd, F_GETFL, 0))) {
			flags = 0;
		}
		return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
		flags = 1;
		return ioctl(fd, FIOBIO, &flags);
#endif
	}
	void purgeGarbageClients() {
		
	}
	bool waitAndServeClients() {
		//epoll_wait and other...
		bool stopped_by_signal = false;
		bool main_cycle_working = true;
		struct epoll_event waiting_events[MIN_WAITING_EVENTS];
		while(main_cycle_working) {
			int eres = epoll_wait(server_epollfd, waiting_events, MIN_WAITING_EVENTS, -1);
			if (-1 == eres) {
				if (EINTR == errno) {
					stopped_by_signal = true;
					break;
				} else {
					error_message = string(strerror(errno));
					break;
				}
			}
			if (0 == eres) continue;	//Timeout
			for (int i = 0; i < eres; ++i) {
				if (waiting_events[i].data.fd == server_socket) {
					//Server has new client or server socket error
					int new_socket = accept(server_socket, 0, 0);
					if (-1 == new_socket) {
						error_message = string(strerror(errno));
						main_cycle_working = false;
						stopped_by_signal = false;
						break;
					}
					setNonBlock(new_socket);
					struct epoll_event new_event;
					new_event.data.fd = new_socket; new_event.events = EPOLLIN | EPOLLHUP | EPOLLERR;
					if (-1 == epoll_ctl(server_epollfd, EPOLL_CTL_ADD, new_socket, &new_event)) {
						//client was rejected
						shutdown(new_socket, SHUT_RDWR);
						close(new_socket);
					}
				} else {
					//Client descriptor has new data or error
					if (((waiting_events[i].events & EPOLLHUP) != 0) || ((waiting_events[i].events & EPOLLERR) != 0)) {
						//client error - rejected
						epoll_ctl(server_epollfd, EPOLL_CTL_DEL, waiting_events[i].data.fd, NULL);
						shutdown(waiting_events[i].data.fd, SHUT_RDWR);
						close(waiting_events[i].data.fd);
						continue;
					}
					//Create client object
					int client_fd = waiting_events[i].data.fd;
					epoll_ctl(server_epollfd, EPOLL_CTL_DEL, client_fd, NULL);
					connections[client_fd] = make_shared<HttpConnection> (client_fd, _directory);
				}
			}
        }//while
        //server was stopped
		close(server_socket);
		close(server_epollfd);
		_running = false;
		server_socket = -1; server_epollfd = -1;
        if (!stopped_by_signal) {
			//Server error
			return false;
		}
		return true;
	}
private:
	string _host_addr;
	uint16_t _port;
	string _directory;
	bool _running;
	string error_message;
	//
	int server_socket;
	int server_epollfd;
	//
	map<int, shared_ptr<HttpConnection>> connections;
};
