//https://bitbucket.org/snippets/vlasovmaksim/krMpr#file-final.md

#include <iostream>
#include "commandparameters.h"
#include "httpserver.h"

using namespace std;

int main(int argc, char **argv) {
	CommandParameters server_options(argc, argv);
	cout << "Host address: " << server_options.getHostAddr() << endl << "Port: " << server_options.getPort() << endl << "File directory: " << server_options.getDirectory() << endl;
	HttpServer server(server_options.getHostAddr(), server_options.getPort(), server_options.getDirectory());
	bool res = server.run();
	if (!res) {
		cout << "Server failed: " << server.getLastError() << endl;
		return -1;
	}
	cout << "Server successufully complete" << endl;
	return 0;
}