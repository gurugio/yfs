// this is the extent server

#ifndef extent_server_h
#define extent_server_h

#include <string>
#include <map>
#include "extent_protocol.h"

class extent_server {
protected:
	struct yfsfile
	{
		yfsfile () {
			file_attr.atime = 0;
			file_attr.mtime = 0;
			file_attr.ctime = 0;
			file_attr.size = 0;
		}
		std::string file_name;
		extent_protocol::attr file_attr;
	};

	std::map<extent_protocol::extentid_t, struct yfsfile *> *file_map;
	pthread_mutex_t attr_lock;

public:
	extent_server();
	int put(extent_protocol::extentid_t id, std::string, int &);
	int get(extent_protocol::extentid_t id, std::string &);
	int getattr(extent_protocol::extentid_t id, extent_protocol::attr &);
#if 0
	int putattr(extent_protocol::extentid_t id, extent_protocol::attr &);
#endif
	int remove(extent_protocol::extentid_t id, int &);
};

#endif
