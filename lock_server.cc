// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "tprintf.h"

lock_server::lock_server():
	nacquire (0)
{
        lock_table = new std::map<lock_protocol::lockid_t,
				  struct local_lock *>;
        pthread_mutex_init(&server_lock, NULL);
	pthread_cond_init(&server_wait, NULL);

	printf("init lock_server\n");
}

lock_server::~lock_server()
{
	std::map<lock_protocol::lockid_t,
		 struct local_lock *>::iterator it;

	it = lock_table->begin();

	while (it != lock_table->end()) {
		delete it->second;
		lock_table->erase(it);
	}
	
	delete lock_table;
}

lock_protocol::status
lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r)
{
	lock_protocol::status ret = lock_protocol::OK;
	struct local_lock *llock;
	std::map<lock_protocol::lockid_t,
		 struct local_lock *>::iterator it;

	printf("ls: stat\n");
	pthread_mutex_lock(&server_lock);
	it = lock_table->find(lid);
	if (it == lock_table->end())
		r = lock_protocol::NOENT;
	else {
		llock = it->second;
		r = llock->status;
	}
	pthread_mutex_unlock(&server_lock);
	return ret;
}

lock_protocol::status
lock_server::acquire(int clt, lock_protocol::lockid_t lid, int &r)
{
	lock_protocol::status ret = lock_protocol::OK;
	struct local_lock *llock;
	std::map<lock_protocol::lockid_t,
		 struct local_lock *>::iterator it;

	fprintf(stdout, "ls: %d-%llu: start acquire\n", clt, lid);
	pthread_mutex_lock(&server_lock);
	nacquire++;

	it = lock_table->find(lid);

	if (it == lock_table->end()) {
		llock = new local_lock();
		llock->status = lock_protocol::FREE;
		lock_table->insert(std::make_pair(lid, llock));
	}

	it = lock_table->find(lid);
	llock = it->second;
	while (llock->status == lock_protocol::LOCKED) {
		pthread_cond_wait(&server_wait, &server_lock);
		// re-read status
		it = lock_table->find(lid);
		llock = it->second;
	}
	r = llock->status = lock_protocol::LOCKED;
	llock->owner = clt;
	pthread_mutex_unlock(&server_lock);

	fprintf(stdout, "ls: %d-%llu: finish acquire\n", clt, lid);
	return ret;
}

lock_protocol::status
lock_server::release(int clt, lock_protocol::lockid_t lid, int &r)
{
	lock_protocol::status ret = lock_protocol::OK;
	struct local_lock *llock;
	std::map<lock_protocol::lockid_t,
		 struct local_lock *>::iterator it;

	tprintf("ls: %d-%llu: start release\n", clt, lid);
	pthread_mutex_lock(&server_lock);

	it = lock_table->find(lid);
	if (it == lock_table->end()) {
		printf("ERROR: no such lock\n");
		r = lock_protocol::NOENT;
		goto unlock;
	}

	llock = it->second;
	if (llock->status != lock_protocol::LOCKED) {
		printf("ERROR: already released\n");
		r = lock_protocol::NOENT;
		goto unlock;
	}
	
	r = llock->status = lock_protocol::FREE;
	llock->owner = 0;

	nacquire--;
	pthread_cond_signal(&server_wait);

unlock:
	pthread_mutex_unlock(&server_lock);

	tprintf("ls: %d-%llu: finish release\n", clt, lid);
	return ret;
}
