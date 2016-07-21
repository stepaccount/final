
#pragma once

#include <getopt.h>
#include <stdint.h>
#include <string>

using std::string;

const string DEFAULT_IP_ADDR = "0.0.0.0";
const uint16_t DEFAULT_PORT = 1022;
const string DEFAULT_DIRECTORY = ".";

class CommandParameters 
{
public:
	CommandParameters(int argc, char **argv) : argc(argc), argv(argv) {
		
		ip_addr_is_set = port_is_set = file_directory_is_set = has_invalid_options = false;
		parseCommandLine();
	}
	string getHostAddr() {return ip_addr;}
	uint16_t getPort() {return port;}
	string getDirectory() {return file_directory;}
	bool hasInvalidOptions() {return has_invalid_options;}
	bool isHostSet() {return ip_addr_is_set;}
	bool isPortSet() {return port_is_set;}
	bool isDirectorySet() {return file_directory_is_set;}
private:
	void parseCommandLine() {
		ip_addr = DEFAULT_IP_ADDR;
		port = DEFAULT_PORT;
		file_directory = DEFAULT_DIRECTORY;
		struct option cl_options[] = {
			{.name = "host", .has_arg = required_argument, .flag = NULL, .val = 'h'},
			{.name = "port", .has_arg = required_argument, .flag = NULL, .val = 'p'},
			{.name = "directory", .has_arg = required_argument, .flag = NULL, .val = 'd'},
			{NULL, 0, NULL, 0}
		};
		const char *optString = "h:p:d:";
		int longIndex;
		int opt;
		opterr = 0;
		opt = getopt_long(argc, argv, optString, cl_options, &longIndex);
		while( opt != -1 ) {
			switch( opt ) {
				case 'h':
					if (optarg == NULL) {
						ip_addr_is_set = false;
					} else {
						ip_addr_is_set = true;
						ip_addr = optarg;
					}
					break;
				case 'p':
					if (optarg == NULL) {
						port_is_set = false;
					} else {
						port_is_set = true;
						port = atoi(optarg);
					}
					break;
				case 'd':
					if (optarg == NULL) {
						file_directory_is_set = false;
					} else {
						file_directory_is_set = true;
						file_directory = optarg;
					}
					break;
				case '?': default:
					has_invalid_options = true;
					break;
			}
			opt = getopt_long(argc, argv, optString, cl_options, &longIndex);
		}

	}
private:
	int argc;
	char **argv;
private:
	string ip_addr;
	uint16_t port;
	string file_directory;
	bool ip_addr_is_set;
	bool port_is_set;
	bool file_directory_is_set;
	bool has_invalid_options;
};