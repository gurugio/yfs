// the extent server implementation

#include "extent_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

static extent_protocol::extentid_t root_id = 1;
static std::string root_name = std::string("root");

extent_server::extent_server()
{
	file_map = new std::map<extent_protocol::extentid_t,
							struct yfsfile *>;

	struct yfsfile *root = new yfsfile();
	root->file_name = root_name;
	root->file_attr.mtime = root->file_attr.atime =
		root->file_attr.ctime = time(NULL);
	file_map->insert(std::make_pair(root_id, root));

	pthread_mutex_init(&attr_lock, NULL);
}

int extent_server::put(extent_protocol::extentid_t id, std::string buf, int &)
{
	printf("es:put:id=%016llx name=%s\n", id, buf.c_str());
	pthread_mutex_lock(&attr_lock);

	std::map<extent_protocol::extentid_t, struct yfsfile *>::iterator it;
	struct yfsfile *f;

	it = file_map->find(id);
	if (it != file_map->end()) {
		// if existing id -> set file-buffer
		f = it->second;
		f->file_attr.mtime = time(NULL);
		f->file_buf = buf;
		f->file_attr.size = buf.length();
	} else {
		// if new id -> create yfsfile object
		f = new yfsfile();
		f->file_name = buf;
		f->file_attr.ctime = f->file_attr.mtime =
			f->file_attr.atime = time(NULL);
		f->file_attr.size = 0;
		file_map->insert(std::make_pair(id, f));
	}

	pthread_mutex_unlock(&attr_lock);
	return extent_protocol::OK;
}

int extent_server::get(extent_protocol::extentid_t id, std::string &buf)
{
	printf("es:get:id=%016llx\n", id);
	pthread_mutex_lock(&attr_lock);

	std::map<extent_protocol::extentid_t, struct yfsfile *>::iterator it;

	it = file_map->find(id);
	if (it == file_map->end()) {
		printf("es:get: no file\n");
		pthread_mutex_unlock(&attr_lock);
		return extent_protocol::NOENT;
	}

	buf = it->second->file_buf;
	it->second->file_attr.atime = time(NULL);
	pthread_mutex_unlock(&attr_lock);
	return extent_protocol::OK;
}

int extent_server::getattr(extent_protocol::extentid_t id, extent_protocol::attr &a)
{
	struct yfsfile *f;
	std::map<extent_protocol::extentid_t, struct yfsfile *>::iterator it;

	printf("es: getattr: id=%llx\n", id);
	pthread_mutex_lock(&attr_lock);

	it = file_map->find(id);
	if (it == file_map->end()) {
		pthread_mutex_unlock(&attr_lock);
		return extent_protocol::NOENT;
	}

	it = file_map->find(id);
	f = it->second;
	f->file_attr.atime = time(NULL);
	a = f->file_attr;

	printf("es: getattr: id=%llx %u %u %u %u\n",
		   id, a.atime, a.mtime, a.ctime, a.size);
	pthread_mutex_unlock(&attr_lock);
	return extent_protocol::OK;
}

int extent_server::remove(extent_protocol::extentid_t id, int &)
{
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

