//https://bitbucket.org/snippets/vlasovmaksim/krMpr#file-final.md

#include <iostream>
#include <unistd.h>
#include <stdio.h>
#include "commandparameters.h"
#include "httpserver.h"

using namespace std;

int main(int argc, char **argv) {
	CommandParameters server_options(argc, argv);
	cout << "Host address: " << server_options.getHostAddr() << endl << "Port: " << server_options.getPort() << endl << "File directory: " << server_options.getDirectory() << endl;
	HttpServer server(server_options.getHostAddr(), server_options.getPort(), server_options.getDirectory());
	int daemon_res = daemon(0, 1);
	if (daemon_res == 0) {
		//child
		//fclose(stdout);
		//fclose(stdin);
		//fclose(stderr);
		bool res = server.run();
		if (!res) {
			cout << "Server failed: " << server.getLastError() << endl;
			return -1;
		}
	}	
	return 0;
}