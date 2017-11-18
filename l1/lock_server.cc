// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>


lock_server::lock_server():
	nacquire (0)
{
	lock_of_names = new std::map<std::string, struct lock_info *>;
	pthread_mutex_init(&map_lock, NULL);
}

lock_protocol::status
lock_server::stat(std::string name, int &r)
{
	lock_protocol::status ret = lock_protocol::OK;
	std::map<std::string, struct lock_info *>::iterator it;
	struct lock_info *info;
	
	printf("stat request: lock-%s\n", name.c_str());
	pthread_mutex_lock(&map_lock);
	it = lock_of_names->find(name);
	if (it != lock_of_names->end()) {
		info = it->second;
		pthread_mutex_unlock(&map_lock);
		r = lock_protocol::OK;
		printf("--> held-by client-%d\n", info->id);
	} else {
		pthread_mutex_unlock(&map_lock);
		r = lock_protocol::NOENT;
		printf("--> no lock with name:%s\n", name.c_str());
	}
	return ret;
}

lock_protocol::status
lock_server::acquire(std::string name, const int id, int &r)
{
	lock_protocol::status ret = lock_protocol::OK;
	std::map<std::string, struct lock_info *>::iterator it;
	struct lock_info *info;

	r = lock_protocol::OK;

	printf("acquire request: lock-%s client-%d\n", name.c_str(), id);

	pthread_mutex_lock(&map_lock);
	it = lock_of_names->find(name);

	if (it == lock_of_names->end()) {
		printf("--> locked: lock-%s client-%d (new)\n",
		       name.c_str(), id);
		info = new lock_info();
		pthread_mutex_init(&info->lock, NULL);
		info->id = id;
		info->locked = 1;
		pthread_mutex_lock(&info->lock);
		lock_of_names->insert(std::make_pair(name, info));
		pthread_mutex_unlock(&map_lock);
	} else {
		pthread_mutex_unlock(&map_lock);

		it = lock_of_names->find(name);
		info = it->second;

		if (info->locked == 1 && info->id == id) {
			printf("--> duplicated locking: lock-%s client-%d\n",
			       name.c_str(), id);
		} else {
			pthread_mutex_lock(&info->lock);
			printf("--> locked: lock-%s client-%d\n",
			       name.c_str(), id);
			info->id = id;
			info->locked = 1;
		}
	}
	return ret;
}

lock_protocol::status
lock_server::release(std::string name, const int id, int &r)
{
	lock_protocol::status ret = lock_protocol::OK;
	std::map<std::string, struct lock_info *>::iterator it;
	struct lock_info *info;

	r = lock_protocol::OK;

	printf("release request: lock-%s client-%d\n", name.c_str(), id);

	pthread_mutex_lock(&map_lock);
	it = lock_of_names->find(name);
	pthread_mutex_unlock(&map_lock);

	info = it->second;
	if (it == lock_of_names->end()) {
		r = lock_protocol::NOENT;
		printf("--> no lock\n");
	} else if (info->id != id) {
		r = lock_protocol::NOENT;
		printf("--> wrong id\n");
	} else {
		info->locked = 0;
		pthread_mutex_unlock(&info->lock);
		printf("--> released: lock-%s client-%d\n",
		       name.c_str(), id);
	}
	//pthread_mutex_unlock(&map_lock);
	return ret;
}
