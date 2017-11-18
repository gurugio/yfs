// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>


lock_server::lock_server():
	nacquire (0)
{
	lock_table = new std::map<std::string, struct named_lock *>;
	pthread_mutex_init(&table_lock, NULL);
}

lock_protocol::status
lock_server::stat(std::string name, int &r)
{
	lock_protocol::status ret = lock_protocol::OK;
	std::map<std::string, struct named_lock *>::iterator it;
	struct named_lock *mylock;
	
	printf("stat request: lock-%s\n", name.c_str());
	pthread_mutex_lock(&table_lock);
	it = lock_table->find(name);
	if (it != lock_table->end()) {
		mylock = it->second;
		pthread_mutex_unlock(&table_lock);
		r = lock_protocol::OK;
		printf("--> held-by client-%d\n", mylock->owner);
	} else {
		pthread_mutex_unlock(&table_lock);
		r = lock_protocol::NOENT;
		printf("--> no lock with name:%s\n", name.c_str());
	}
	return ret;
}

lock_protocol::status
lock_server::acquire(std::string name, const int id, int &r)
{
	lock_protocol::status ret = lock_protocol::OK;
	std::map<std::string, struct named_lock *>::iterator it;
	std::set<int>::iterator it_wait;
	struct named_lock *mylock;

	r = 0;

	printf("acquire request: lock-%s client-%d\n", name.c_str(), id);

	pthread_mutex_lock(&table_lock);
	it = lock_table->find(name);

	if (it == lock_table->end()) {
		printf("--> locked(new): lock-%s client-%d\n",
		       name.c_str(), id);
		//mylock = new_named_lock(name.c_str(), id);
		mylock = new named_lock();
		pthread_mutex_init(&mylock->lock, NULL);
		mylock->owner = id;
		mylock->name = name;
		lock_table->insert(std::make_pair(name, mylock));
		pthread_mutex_unlock(&table_lock);
		return ret;
	}
	pthread_mutex_unlock(&table_lock);

	mylock = it->second;
	pthread_mutex_lock(&mylock->lock);

retry:
	if (mylock->owner == -1) {
		printf("--> locked(single): lock-%s client-%d\n",
		       name.c_str(), id);
		mylock->owner = id;
		pthread_mutex_unlock(&mylock->lock);
		return ret;
	}

	if (mylock->owner == id) {
		printf("--> locked(duplicated): lock-%s client-%d\n",
		       name.c_str(), id);
		mylock->wait_list.erase(id);
		pthread_mutex_unlock(&mylock->lock);
		r = -2;
		return ret;
	}

	it_wait = mylock->wait_list.find(id);
	if (it_wait != mylock->wait_list.end()) {
		printf("--> wait(duplicated): lock-%s client-%d\n",
		       name.c_str(), id);
	} else {
		// now this thread should wait
		mylock->wait_list.insert(id);
		printf("--> wait: lock-%s client-%d\n", name.c_str(), id);
	}
	pthread_cond_wait(&mylock->wait_cond, &mylock->lock);
	goto retry;
	// never reach here
	return ret;
}

lock_protocol::status
lock_server::release(std::string name, const int id, int &r)
{
	lock_protocol::status ret = lock_protocol::OK;
	std::map<std::string, struct named_lock *>::iterator it;
	struct named_lock *mylock;

	r = 0;
	printf("release request: lock-%s client-%d\n", name.c_str(), id);

	it = lock_table->find(name);
	mylock = it->second;
	if (it == lock_table->end()) {
		r = -1;
		printf("--> ERROR: try to release unknown lock\n");
		return ret;
	}
	if (mylock->owner != id) {
		r = -2;
		printf("--> ERROR: try to release other<%d>'s lock\n",
			mylock->owner);
		return ret;
	}
	if (mylock->owner == -1) {
		r = -3;
		printf("--> ERROR: try to release unlocked lock\n");
		return ret;
	}

	printf("--> released: lock-%s client-%d\n",
	       name.c_str(), id);

	pthread_mutex_lock(&mylock->lock);
	mylock->owner = -1;
	pthread_cond_broadcast(&mylock->wait_cond);
	pthread_mutex_unlock(&mylock->lock);
	return ret;
}
