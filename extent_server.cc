// the extent server implementation

#include "extent_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

extent_server::extent_server()
{
	file_map = new std::map<extent_protocol::extentid_t,
				struct yfsfile *>;
	pthread_mutex_init(&attr_lock, NULL);
}


int extent_server::put(extent_protocol::extentid_t id, std::string buf, int &)
{
	// You fill this in for Lab 2.
	struct yfsfile *f;
	std::map<extent_protocol::extentid_t, struct yfsfile *>::iterator it;

	pthread_mutex_lock(&attr_lock);

	it = file_map->find(id);
	if (it == file_map->end()) {
		f = new yfsfile();
		file_map->insert(std::make_pair(id, f));
	}

	it = file_map->find(id);
	f = it->second;
	f->file_name = buf;
	f->file_attr.ctime = f->file_attr.mtime = time(NULL);

	pthread_mutex_unlock(&attr_lock);
	return extent_protocol::OK;
}

int extent_server::get(extent_protocol::extentid_t id, std::string &buf)
{
	// You fill this in for Lab 2.
	struct yfsfile *f;
	std::map<extent_protocol::extentid_t, struct yfsfile *>::iterator it;

	pthread_mutex_lock(&attr_lock);

	it = file_map->find(id);
	if (it == file_map->end()) {
		return extent_protocol::IOERR;
	}

	it = file_map->find(id);
	f = it->second;
	buf = f->file_name;
	f->file_attr.atime = time(NULL);

	pthread_mutex_unlock(&attr_lock);
	return extent_protocol::OK;
}

int extent_server::getattr(extent_protocol::extentid_t id,
			   extent_protocol::attr &a)
{
	// You fill this in for Lab 2.
	// You replace this with a real implementation. We send a phony response
	// for now because it's difficult to get FUSE to do anything (including
	// unmount) if getattr fails.
	struct yfsfile *f;
	std::map<extent_protocol::extentid_t, struct yfsfile *>::iterator it;

	pthread_mutex_lock(&attr_lock);

	it = file_map->find(id);
	if (it == file_map->end()) {
		return extent_protocol::IOERR;
	}

	it = file_map->find(id);
	f = it->second;
	a = f->file_attr;

	pthread_mutex_unlock(&attr_lock);
	return extent_protocol::OK;
}

int extent_server::remove(extent_protocol::extentid_t id, int &)
{
	// You fill this in for Lab 2.
	struct yfsfile *f;
	std::map<extent_protocol::extentid_t, struct yfsfile *>::iterator it;

	pthread_mutex_lock(&attr_lock);

	it = file_map->find(id);
	if (it == file_map->end()) {
		return extent_protocol::IOERR;
	}

	it = file_map->find(id);
	f = it->second;

	file_map->erase(it);
	free(f);

	pthread_mutex_unlock(&attr_lock);
	return extent_protocol::OK;
}

