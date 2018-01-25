// the caching lock server implementation

#include "lock_server_cache.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lang/verify.h"
#include "handle.h"
#include "tprintf.h"


lock_server_cache::lock_server_cache()
{
	nacquire = 0;
	lock_table = new std::map<lock_protocol::lockid_t,
							  struct local_lock *>;
	pthread_mutex_init(&server_lock, NULL);
	pthread_cond_init(&server_wait, NULL);
}

int lock_server_cache::call_revoke(lock_protocol::lockid_t lid, std::string id)
{
	int ret, r;
	rpcc *cl;
	sockaddr_in dstsock;

	make_sockaddr(id.c_str(), &dstsock);
	cl = new rpcc(dstsock);
	if (cl->bind() < 0) {
		printf("lock_server_cache: call bind\n");
	}

	ret = cl->call(rlock_protocol::revoke, lid, r);
	if (ret != rlock_protocol::OK)
		tprintf("lsc: revoke RPC error\n");

	delete cl;
	return ret;
}

int lock_server_cache::acquire(lock_protocol::lockid_t lid, std::string id, 
							   int &r)
{
	struct local_lock *llock;
	std::map<lock_protocol::lockid_t,
			 struct local_lock *>::iterator it;

	tprintf("lsc: %s-%llu: start acquire\n", id.c_str(), lid);

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

	if (llock->status == lock_protocol::LOCKED) {
		llock->wait_list.push_back(id);
		r = lock_protocol::RETRY;
		call_revoke(lid, llock->owner);
	} else {
		r = llock->status = lock_protocol::LOCKED;
		llock->owner = id;
	}

	tprintf("lsc: %s-%llu: finish acquire\n", id.c_str(), lid);
	pthread_mutex_unlock(&server_lock);
	return lock_protocol::OK;
}

int lock_server_cache::call_retry(lock_protocol::lockid_t lid, std::string id)
{
	int ret, r;
	rpcc *cl;
	sockaddr_in dstsock;

	make_sockaddr(id.c_str(), &dstsock);
	cl = new rpcc(dstsock);
	if (cl->bind() < 0) {
		printf("lock_server_cache: call bind\n");
	}

	ret = cl->call(rlock_protocol::retry, lid, r);
	if (ret != rlock_protocol::OK)
		tprintf("lsc: revoke RPC error\n");

	delete cl;
	return ret;
}

int 
lock_server_cache::release(lock_protocol::lockid_t lid, std::string id, 
		 int &r)
{
	struct local_lock *llock;
	std::map<lock_protocol::lockid_t,
			 struct local_lock *>::iterator it;
	std::string next_owner;

	tprintf("ls: %s-%llu: start release\n", id.c_str(), lid);

	pthread_mutex_lock(&server_lock);

	it = lock_table->find(lid);
	llock = it->second;

	r = llock->status = lock_protocol::FREE;
	llock->owner = "";
	nacquire--;

	// TODO: send retry to all waiting-threads
	// then waiting-threads send revoke again to new owner??
	if (!llock->wait_list.empty()) {
	    next_owner = llock->wait_list.front();
	    llock->wait_list.pop_front();
	    call_retry(lid, next_owner);
	    tprintf("ls: %s-%llu: send-retry to %s\n", id.c_str(), lid, next_owner.c_str());
	}

	tprintf("ls: %s-%llu: finish release\n", id.c_str(), lid);
	pthread_mutex_unlock(&server_lock);
	return lock_protocol::OK;
}

lock_protocol::status
lock_server_cache::stat(lock_protocol::lockid_t lid, int &r)
{
	struct local_lock *llock;
	std::map<lock_protocol::lockid_t,
			 struct local_lock *>::iterator it;

	tprintf("lsc: stat request: lid=%llu\n", lid);
	pthread_mutex_lock(&server_lock);

	it = lock_table->find(lid);
	if (it == lock_table->end())
		r = lock_protocol::NOENT;
	else {
		llock = it->second;
		r = llock->status;
	}

	pthread_mutex_unlock(&server_lock);
	return lock_protocol::OK;
}

