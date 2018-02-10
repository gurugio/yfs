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
	struct yfsfile *f;

	file_map = new std::map<extent_protocol::extentid_t,
							struct yfsfile *>;
	pthread_mutex_init(&attr_lock, NULL);

	// create root
	f = new yfsfile();
	f->file_name = "/";
	f->file_attr.ctime = f->file_attr.mtime =
		f->file_attr.atime = time(NULL);
	f->file_attr.size = 0;
	f->file_buf = "";
	file_map->insert(std::make_pair(1, f));
}

int extent_server::put(extent_protocol::extentid_t id, std::string buf, int &)
{
	std::map<extent_protocol::extentid_t, struct yfsfile *>::iterator it;
	struct yfsfile *f;

	printf("es:put:id=%016llx\n", id);
	pthread_mutex_lock(&attr_lock);

	it = file_map->find(id);
	if (it != file_map->end()) {
		printf("es:put: set buf\n");
		// if existing id -> set file-buffer
		f = it->second;
		f->file_attr.mtime = f->file_attr.ctime = time(NULL);
		f->file_buf = buf;
		f->file_attr.size = buf.length();
	} else {
		printf("es:put: create new file\n");
		// if new id -> create yfsfile object
		f = new yfsfile();
		f->file_buf = buf;
		f->file_attr.ctime = f->file_attr.mtime =
			f->file_attr.atime = time(NULL);
		f->file_attr.size = buf.length();
		file_map->insert(std::make_pair(id, f));
	}

	pthread_mutex_unlock(&attr_lock);
	printf("es:put:OK\n");
	return extent_protocol::OK;
}

int extent_server::get(extent_protocol::extentid_t id, std::string &buf)
{
	std::map<extent_protocol::extentid_t, struct yfsfile *>::iterator it;

	printf("es:get:id=%016llx\n", id);
	pthread_mutex_lock(&attr_lock);

	it = file_map->find(id);
	if (it == file_map->end()) {
		printf("es:get: no file\n");
		pthread_mutex_unlock(&attr_lock);
		return extent_protocol::NOENT;
	}

	buf = it->second->file_buf;
	it->second->file_attr.atime = time(NULL);
	pthread_mutex_unlock(&attr_lock);
	printf("es:get:OK\n");
	return extent_protocol::OK;
}

int extent_server::getattr(extent_protocol::extentid_t id, extent_protocol::attr &a)
{
	struct yfsfile *f;
	std::map<extent_protocol::extentid_t, struct yfsfile *>::iterator it;

	printf("es:getattr: id=%016llx\n", id);
	pthread_mutex_lock(&attr_lock);

	it = file_map->find(id);
	if (it == file_map->end()) {
		printf("es:getattr: NOENT\n");
		pthread_mutex_unlock(&attr_lock);
		return extent_protocol::NOENT;
	}

	it = file_map->find(id);
	f = it->second;
	f->file_attr.atime = time(NULL);
	a = f->file_attr;

	printf("es:getattr: id=%llx %u %u %u %u\n",
		   id, a.atime, a.mtime, a.ctime, a.size);
	pthread_mutex_unlock(&attr_lock);
	return extent_protocol::OK;
}

int extent_server::remove(extent_protocol::extentid_t id, int &)
{
	struct yfsfile *f;
	std::map<extent_protocol::extentid_t, struct yfsfile *>::iterator it;

	printf("es:remove: id=%016llx\n", id);
	pthread_mutex_lock(&attr_lock);

	it = file_map->find(id);
	if (it == file_map->end()) {
		printf("es:remove: NOENT\n");
		pthread_mutex_unlock(&attr_lock);
		return extent_protocol::IOERR;
	}

	it = file_map->find(id);
	f = it->second;

	file_map->erase(it);
	delete f;

	printf("es:remove: done\n");

#if DEBUG
	it = file_map->find(id);
	if (it == file_map->end()) {
		printf("es:remove: REMOVING FAILED!!!\n");
		pthread_mutex_unlock(&attr_lock);
		return extent_protocol::IOERR;
	}
#endif
	pthread_mutex_unlock(&attr_lock);
	return extent_protocol::OK;
}

