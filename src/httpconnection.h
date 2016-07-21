
#pragma once

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <sstream>
#include <vector>
#include <unordered_set>
#include <thread>
#include <mutex>

//DEBUF
#include <iostream>
using std::cout;

using std::string;
using std::thread;
using std::mutex;
using std::vector;
using std::stringstream;
//using std::unordered_set;

class HttpConnection 
{
public:
	typedef enum {httpINVALID, httpGET} httprequest_t;
	struct HttpRequest {
		httprequest_t type;
		string version;
		string uri;
		string host;
		string accept;
		string user_agent;
	};
	typedef enum {noAnswerPending, fileAnswer, memoryAnswer} httpanswer_t;
	struct HttpAnswerOperation {
		httpanswer_t type;
		int fd;
		size_t fd_need_to_write;
		bool stream_written;
		stringstream stream;
		HttpAnswerOperation() : type(noAnswerPending), fd(-1), fd_need_to_write(0), stream_written(false) {}
	};
public:
	const int HTTP_CONNECTION_TIMEOUT = 10000;	//ms
	const int HTTP_BUFFER_MAX_SIZE = 4096;
	const int HTTP_BUFFER_MIN_SIZE = 1024;
public:
	HttpConnection(int sock, string &directory) : conn_socket(sock), work_dir(directory) {
		//allowed_headers = {"Host", "Accept"};
		connection_completed = true; 
		epollfd = epoll_create1(0);
		if (-1 == epollfd) {
			return;
		}
		struct epoll_event ev; ev.data.fd = conn_socket; ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
		if (-1 == epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_socket, &ev)) {
			close(epollfd);
			return;
		}
		connection_timeout = HTTP_CONNECTION_TIMEOUT;
		connection_buffer.reserve(HTTP_BUFFER_MIN_SIZE);
		connection_completed = false;
		connection_thread = thread(HttpConnection::thread_function, this);
	}
	~HttpConnection() {
		if (!isCompleted()) {
			shutdown(conn_socket, SHUT_RDWR);
			close(conn_socket);
		}
		if (connection_thread.joinable()) connection_thread.join();
	}
	bool isCompleted() {
		return connection_completed;
	}
private:
	void set_completed(bool val) {connection_completed = val;}
	
	bool append_to_buffer(char * buffer, size_t size) {
		if (connection_buffer.size() + size > HTTP_BUFFER_MAX_SIZE) return false;
		if (connection_buffer.size() > 0) connection_buffer.pop_back();
		connection_buffer.insert(connection_buffer.end(), buffer, buffer + size);
		connection_buffer.push_back('\0');
		return true;
	}
	bool parseHttpMainHeader(const char *header, struct HttpRequest &request) {
		
		const char * find_request = "GET";
		const char * version_string = "HTTP/";
		char delimiter = '\x20';
		const char * cur_pos = header;
		
		request.uri.clear();
		request.version.clear();
		if (0 != strncmp(cur_pos, find_request, strlen(find_request))) {
			request.type = httpINVALID;
			return false;
		}
		cur_pos += strlen(find_request);
		if (delimiter != cur_pos[0]) {
			request.type = httpINVALID;
			return false;
		}
		++cur_pos;
		const char * tmp_pos = index(cur_pos, delimiter);
		if (NULL == tmp_pos) {
			request.type = httpINVALID;
			return false;
		}
		request.uri = string(cur_pos, (size_t)(tmp_pos - cur_pos));
		cur_pos = tmp_pos+1;
		
		if (0 != strncmp(cur_pos, version_string, strlen(version_string))) {
			request.type = httpINVALID;
			return false;			
		}
		cur_pos += strlen(version_string);
		request.version = string(cur_pos);
		request.type = httpGET;
		return true;
	}
	bool parseHttpHeaderString(const char *header_string, struct HttpRequest &request) {
		const char * value;
		const char delimiter = ':';
		
		const char * d_pos = index(header_string, delimiter);
		if (NULL == d_pos) return false;
		string header(header_string, size_t(d_pos - header_string));
		//auto fnd = allowed_headers.find(header);
		//if (allowed_headers.end() == fnd) return true; systemError
		while (*(++d_pos) == '\x20');
		value = d_pos;
		if ("Host" == header) {
			request.host = string(value);
			return true;
		} 
		if ("Accept" == header) {
			request.accept = string(value);
			return true;
		}
		if ("User-Agent" == header) {
			request.user_agent = string(value);
			return true;
		}
		return true;
	}
	bool parseHttpRequest(struct HttpRequest &request) {
		const int MAX_REQUESTNAME_SIZE = 3;
		const int MIN_REQUEST_SIZE = 16;
		const char * find_request = "GET";
		const char * end_request = "\r\n\r\n";
		const char * string_delimiter = "\r\n";
		
		int parsed_bytes = 0;

		char * buf_begin = connection_buffer.data();
		size_t buf_size = connection_buffer.size();
		if (buf_size - 1 < MIN_REQUEST_SIZE) {
			return false;
		}
		char * reqname_pos = strstr(buf_begin, find_request);
		if (NULL == reqname_pos) {
			parsed_bytes = buf_size - MAX_REQUESTNAME_SIZE -1;
			connection_buffer.erase(connection_buffer.begin(), connection_buffer.begin() + parsed_bytes);
			return false;
		}
		parsed_bytes = reqname_pos - buf_begin;
		char * end_pos = strstr(reqname_pos, end_request);
		if (NULL == end_pos) {
			connection_buffer.erase(connection_buffer.begin(), connection_buffer.begin() + parsed_bytes);
			return false;
		}
		//Request fully received
		parsed_bytes = end_pos + strlen(end_request) - buf_begin;
		char * cur_pos = reqname_pos; //to GET
		char * cur_find;
		int str_num = 0;
		request.type = httpINVALID;
		while (NULL != (cur_find = strstr(cur_pos, string_delimiter))) {
			cur_find[0] = '\0';
			if (0 == str_num) {
				//Main header string
				if (!parseHttpMainHeader(cur_pos, request)) {
					break;
				}
				++str_num;
				cur_pos = cur_find + strlen(string_delimiter);
				continue;
			}
			if (0 == strlen(cur_pos)) {
				//End of request
				break;
			}
			//Header string
			if(!parseHttpHeaderString(cur_pos, request)) {
				request.type = httpINVALID;
				break;
			}
			cur_pos = cur_find + strlen(string_delimiter);
			++str_num;
		}
		connection_buffer.erase(connection_buffer.begin(), connection_buffer.begin() + parsed_bytes);
		return true;
	}
	bool handleRequest(const struct HttpRequest &request, struct HttpAnswerOperation &op) {
		if (httpINVALID == request.type) {
			op.type = memoryAnswer;
			createBadRequestAnswer(op.stream);
			return true;
		}
		if (httpGET == request.type) {
			createFileAnswer(request, op);
			return true;
		}
	}
	void createFileAnswer(const struct HttpRequest &request, struct HttpAnswerOperation &op) {
		size_t par_pos = request.uri.find('?');
		string filepath(".");		
		if (par_pos != string::npos) {
			filepath.append(request.uri, 0, par_pos);
		} else {
			filepath.append(request.uri);
		}
		//DEBUG
		cout << "DEBUG: requesting file " << filepath << " socket:" << conn_socket << std::endl;
		//
		int fd = open(filepath.c_str(), O_RDONLY | O_NOCTTY);
		if (-1 == fd) {
			op.type = memoryAnswer;
			if (errno == EACCES) {
				createForbiddenAnswer(op.stream);
				return;
			}
			if (errno == ENOENT) {
				createNotFoundAnswer(op.stream);
				return;
			}
			//Unknown error
			createInternalServerError(op.stream);
			return;
		}
		//file was opened
		struct stat file_stat;
		if (-1 == fstat(fd, &file_stat)) {
			close(fd);
			op.type = memoryAnswer;
			createInternalServerError(op.stream);
			return;
		}
		if (!S_ISREG(file_stat.st_mode)) {
			close(fd);
			op.type = memoryAnswer;
			createNotFoundAnswer(op.stream);
			return;
		}
		size_t f_size = file_stat.st_size;
		op.type = fileAnswer;
		createOkAnswer(op.stream, f_size);
		op.fd = fd;
		op.stream_written = false;
		op.fd_need_to_write = f_size;
		if (f_size == 0) {
			op.type = memoryAnswer;
			close(fd);
		}
	}
	void createOkAnswer(stringstream &ans, size_t body_size) {	
		ans.str(""); ans.clear();
		ans << "HTTP/1.0 200 OK" << "\r\n";
		ans << "Content-length: "<< body_size << "\r\n";
		ans << "Content-Type: text/html";
		ans << "\r\n\r\n";
	}
	void createBadRequestAnswer(stringstream &ans) {	
		ans.str(""); ans.clear();
		ans << "HTTP/1.0 400 BAD REQUEST" << "\r\n";
		ans << "Content-length: 0" << "\r\n";
		ans << "Content-Type: text/html";
		ans << "\r\n\r\n";
	}
	void createNotFoundAnswer(stringstream &ans) {
		ans.str(""); ans.clear();
		ans << "HTTP/1.0 404 NOT FOUND" << "\r\n";
		ans << "Content-length: 0" << "\r\n";
		ans << "Content-Type: text/html";
		ans << "\r\n\r\n";
	}
	void createForbiddenAnswer(stringstream &ans) {
		ans.str(""); ans.clear();
		ans << "HTTP/1.0 403 FORBIDDEN" << "\r\n";
		ans << "Content-length: 0" << "\r\n";
		ans << "Content-Type: text/html";
		ans << "\r\n\r\n";		
	}
	void createInternalServerError(stringstream &ans) {
		ans.str(""); ans.clear();
		ans << "HTTP/1.0 501 Internal Server Error" << "\r\n";
		ans << "Content-length: 0" << "\r\n";
		ans << "Content-Type: text/html";
		ans << "\r\n\r\n";		
	}
	bool write_answer_to_socket(struct HttpAnswerOperation &op, bool &system_error) {
		if (op.type == noAnswerPending) {
			//Programming error
			system_error = true;
			return false;
		}
		op.stream.seekg (0, op.stream.end);
		size_t cur_length = op.stream.tellg();
		op.stream.seekg (0, op.stream.beg);
		ssize_t written = send(conn_socket, op.stream.str().c_str(), cur_length, MSG_NOSIGNAL);
		if ((written <= 0) && (errno != EAGAIN)) {
			system_error = true;
			op.type = noAnswerPending;
			return false;
		}
		if (written != cur_length) {
			op.stream.ignore(written);
			system_error = false;
			return false;
		}
		op.stream_written = true;
		if (op.type == memoryAnswer) {
			system_error = false;
			op.type = noAnswerPending;
			return true;
		}
		if (op.type == fileAnswer) {
			ssize_t send_res = sendfile(conn_socket, op.fd, NULL, op.fd_need_to_write);
			if ((send_res <= 0) && (errno != EAGAIN)) {
				system_error = true;
				op.type = noAnswerPending;
				close(op.fd);
				return false;
			}
			op.fd_need_to_write -= send_res;
			if (op.fd_need_to_write == 0) {
				//all sended
				system_error = false;
				op.type = noAnswerPending;
				close(op.fd);
				return true;
			}
		}
	}
	static void thread_function(HttpConnection * conn) {
		cout << "Hello, i am new thread for socket: " << conn->conn_socket << std::endl;
		struct HttpAnswerOperation ans_op;
		while(true) {
			struct epoll_event ev;	
			int eres = epoll_wait(conn->epollfd, &ev, 1, conn->connection_timeout);
			if (-1 == eres) {
				cout << "DEBUG: epoll_wait error for socket: " << conn->conn_socket << std::endl;
				break;
			}
			if (0 == eres) {
				//TimeoutHttpRequest_t
				cout << "DEBUG: epoll_wait timeout for socket: " << conn->conn_socket << std::endl;
				break;
			}
			//socket ready
			int ready_socket = ev.data.fd;
			if (((ev.events & EPOLLHUP) != 0) || ((ev.events & EPOLLERR) != 0)) {
				cout << "DEBUG: Socket error event flag detected for socket: " << conn->conn_socket << std::endl;
				break;
			}
			if ((ev.events & EPOLLOUT) != 0) {
				bool system_error;
				bool wres = conn->write_answer_to_socket(ans_op, system_error);
				if (wres) { //Operation fully complete
					struct epoll_event modev;
					modev.data.fd = ready_socket;
					modev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
					if (-1 == epoll_ctl(conn->epollfd, EPOLL_CTL_MOD, ready_socket, &modev)) {
						cout << "DEBUG: epoll modification error, socket: " << ready_socket << std::endl;
						break;
					}
					cout << "DEBUG: Answer sended, socket: " << ready_socket << std::endl;
				} else {
					if (system_error) {
						cout << "DEBUG: system write error, socket: " << ready_socket << std::endl;
						break;
					}
					//Operation partially complete
					continue;
				}
			} //EPOLLOUT
			if ((ev.events & EPOLLIN) != 0) {
				char t_buffer[conn->HTTP_BUFFER_MIN_SIZE];
				ssize_t readed = recv(ready_socket, t_buffer, conn->HTTP_BUFFER_MIN_SIZE, 0);
				if ((readed == 0) && (errno != EAGAIN)) {
					cout << "DEBUG: Socket was disconnected, socket: " << conn->conn_socket << std::endl;
					break;				
				}
				if (readed < 0) {
					cout << "DEBUG: Read socket error, socket: " << conn->conn_socket << std::endl;
					break;				
				}
				if (!conn->append_to_buffer(t_buffer, readed)) {
					cout << "DEBUG: input buffer overload, socket: " << conn->conn_socket << std::endl;
					break;								
				}
				//ready to parse
				struct HttpRequest cur_request;
				if (!conn->parseHttpRequest(cur_request)) {
					//DEBUG
					cout << "DEBUG: Http request not detected, socket: " << conn->conn_socket << std::endl;
				}
				conn->last_request = cur_request;
				
				//DEBUG:
				cout << "Http request detected\n";
				cout << ((cur_request.type == httpGET) ? "Type: GET" : "Type: Invalid") << std::endl;
				cout << "URI: " << cur_request.uri << std::endl;
				cout << "Version: " << cur_request.version << std::endl;
				cout << "Host: " << cur_request.host << std::endl;
				cout << "Accept: " << cur_request.accept << std::endl;
				cout << "User-Agent: " << cur_request.user_agent << std::endl;
				//
				
				if (conn->handleRequest(cur_request, ans_op)) {
					struct epoll_event modev;
					modev.data.fd = ready_socket;
					modev.events = EPOLLOUT | EPOLLHUP | EPOLLERR;
					if (-1 == epoll_ctl(conn->epollfd, EPOLL_CTL_MOD, ready_socket, &modev)) {
						cout << "DEBUG: epoll modification error, socket: " << ready_socket << std::endl;
						break;
					}
				}
			} //EPOLLIN
		}	
		cout << "Thread complete for socket: " << conn->conn_socket << std::endl;
		//???
		shutdown(conn->conn_socket, SHUT_RDWR);
		close(conn->conn_socket);
		conn->conn_socket = -1;
		close(conn->epollfd);
		conn->epollfd = -1;
		conn->set_completed(true);
	}
private:
	int conn_socket;
	string work_dir;
	int epollfd;
	thread connection_thread;
	bool connection_completed;
	int connection_timeout;
	vector<char> connection_buffer;
	//
	struct HttpRequest last_request;
	//unordered_set<string> allowed_headers;
};