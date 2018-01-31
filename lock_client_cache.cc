// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include "tprintf.h"

#undef tprintf
#define tprintf(args...) do {} while (0);


lock_client_cache::lock_client_cache(std::string xdst, 
				     class lock_release_user *_lu)
	: lock_client(xdst), lu(_lu)
{
	rpcs *rlsrpc = new rpcs(0);
	rlsrpc->reg(rlock_protocol::revoke, this,
				&lock_client_cache::revoke_handler);
	rlsrpc->reg(rlock_protocol::retry, this,
				&lock_client_cache::retry_handler);

	const char *hname;
	hname = "127.0.0.1";
	std::ostringstream host;
	host << hname << ":" << rlsrpc->port();
	id = host.str();

	// Lab4 code
	tprintf("Create a client: id=%s\n", id.c_str());
	pthread_mutex_init(&client_lock, NULL);
	pthread_cond_init(&client_wait, NULL);
	to_release = false;
	// client also manages multiple locks
	lock_table = new std::map<lock_protocol::lockid_t,
				  struct local_lock *>;
	nacquire = 0;
}

lock_protocol::status
lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
	int ret = lock_protocol::OK;
	int r;
	struct local_lock *llock;
	std::map<lock_protocol::lockid_t,
			 struct local_lock *>::iterator it;

	tprintf("lcc: %s-%llu: start acquire\n", id.c_str(), lid);
	pthread_mutex_lock(&client_lock);

	nacquire++;

	it = lock_table->find(lid);

	if (it == lock_table->end()) {
		llock = new local_lock();
		llock->status = LOCK_NONE;
		// pthread_mutex_init(&llock->retry_lock, NULL);
		pthread_cond_init(&llock->retry_wait, NULL);
		lock_table->insert(std::make_pair(lid, llock));
	}

	it = lock_table->find(lid);
	llock = it->second;

	while (llock->status == LOCK_LOCKED
		   || llock->status == LOCK_ACQUIRING
		   || llock->status == LOCK_RELEASING) {
		tprintf("lcc: %s-%llu: wait\n", id.c_str(), lid);
		pthread_cond_wait(&client_wait, &client_lock);
		// wake up and re-check: LOCK_NONE or LOCK_FREE
		it = lock_table->find(lid);
		llock = it->second;

		if (llock->status == LOCK_FREE || llock->status == LOCK_NONE) {
			tprintf("lcc: %s-%llu: go\n", id.c_str(), lid);
		} else {
			tprintf("lcc: %s-%llu: wait again\n", id.c_str(), lid);
		}
	}
  
	if (llock->status == LOCK_NONE) {
		// change status, other threads will wait cond
		llock->status = LOCK_ACQUIRING;

	retry_acquire:
		tprintf("lcc: %s-%llu: call RPC-acquire\n", id.c_str(), lid);
		// MUST unlock before RPC call

		llock->waiting_retry = false;
		pthread_mutex_unlock(&client_lock);

		ret = cl->call(lock_protocol::acquire_cache, lid, id, r);
		VERIFY (ret == lock_protocol::OK);
		
		pthread_mutex_lock(&client_lock);

		if (r == lock_protocol::LOCKED) {
			llock->owner = pthread_self();
			tprintf("lcc: %s-%llu: acquire-OK\n", id.c_str(), lid);
		} else if (r == lock_protocol::RETRY) {
			tprintf("lcc: %s-%llu: acquire-RETRY\n", id.c_str(), lid);
			// wait server sends retry rpc and retry_handler is called
			if (llock->waiting_retry)
				goto retry_acquire;

			llock->waiting_retry = true;
			pthread_cond_wait(&llock->retry_wait, &client_lock);
			goto retry_acquire;
		} else {
			printf("lcc: %s-%llu: ERROR rpc failed\n", id.c_str(), lid);
			goto rpc_error;
		}

		llock->status = LOCK_LOCKED;
	} else if (llock->status == LOCK_FREE) {
		tprintf("lcc: %s-%llu: get local-lock\n", id.c_str(), lid);
		llock->owner = pthread_self();
		llock->status = LOCK_LOCKED;
	} else {
		printf("lcc: %s-%llu: ERROR llock->status:%d\n",
				id.c_str(), lid, llock->status);
		exit(1);
	}

	tprintf("lcc: %s-%llu: finish acquire\n", id.c_str(), lid);
	pthread_mutex_unlock(&client_lock);

	return lock_protocol::OK;
rpc_error:
	nacquire--;
	pthread_mutex_unlock(&client_lock);
	return lock_protocol::RPCERR;
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
	int ret, r;
	struct local_lock *llock;
	std::map<lock_protocol::lockid_t,
			 struct local_lock *>::iterator it;

	pthread_mutex_lock(&client_lock);
	tprintf("lcc: %s-%llu: start release\n", id.c_str(), lid);

	nacquire--;
	tprintf("lcc: %s-%llu: other threads=%d\n", id.c_str(), lid, nacquire);

	it = lock_table->find(lid);
	llock = it->second;

	// BUGBUG: if no thread is waiting, it MUST release lock to server
	
	if (to_release || nacquire == 0 /* no need to keep lock */) {
		llock->status = LOCK_RELEASING;
		tprintf("lcc: %s-%llu: call RPC-release\n", id.c_str(), lid);
		pthread_mutex_unlock(&client_lock);

		ret = cl->call(lock_protocol::release_cache, lid, id, r);
		VERIFY(ret == lock_protocol::OK);

		pthread_mutex_lock(&client_lock);
		llock->status = LOCK_NONE;
		to_release = false;

		/* If there is waiting thread, this makes it try to acquire
		 * Lock is released to other clients. But there could some threads
		 * waiting for client-lock. They don't call acquire-rpc yet.
		 * They are not registered as waiting client in lock server.
		 * So it must wake threads waiting for client-wait, then they
		 * will try acquire and get lock back.
		 */
		if (nacquire) {
			tprintf("lcc: %s-%llu: wake waiting thread\n", id.c_str(), lid);
			pthread_cond_signal(&client_wait);
		}
	} else {
		pthread_cond_signal(&client_wait);
		llock->status = LOCK_FREE;
		tprintf("lcc: %s-%llu: keep lock\n", id.c_str(), lid);
	}

	llock->owner = "";

	tprintf("lcc: %s-%llu: finish release\n", id.c_str(), lid);
	pthread_mutex_unlock(&client_lock);
	
	return lock_protocol::OK;
}

rlock_protocol::status
lock_client_cache::revoke_handler(lock_protocol::lockid_t lid, 
								  int &)
{
	struct local_lock *llock;
	std::map<lock_protocol::lockid_t,
			 struct local_lock *>::iterator it;
	int ret;
	int r;

	pthread_mutex_lock(&client_lock);
	tprintf("lcc: %s-%llu: start revoke_handler\n", id.c_str(), lid);

	it = lock_table->find(lid);
	llock = it->second;
	if (llock->status == LOCK_NONE) {
		printf("lcc: %s-%llu: ERROR, not my lock\n", id.c_str(), lid);
	} else if (llock->status == LOCK_FREE) {
		// no thread is holding lock, so it can be released
		llock->status = LOCK_RELEASING;
		tprintf("lcc: %s-%llu: release to server\n", id.c_str(), lid);
		pthread_mutex_unlock(&client_lock);

		ret = cl->call(lock_protocol::release_cache, lid, id, r);
		VERIFY(ret == lock_protocol::OK);

		pthread_mutex_lock(&client_lock);
		llock->status = LOCK_NONE;
		to_release = false;

		/* If there is waiting thread, this makes it try to acquire
		 * Lock is released to other clients. But there could some threads
		 * waiting for client-lock. They don't call acquire-rpc yet.
		 * They are not registered as waiting client in lock server.
		 * So it must wake threads waiting for client-wait, then they
		 * will try acquire and get lock back.
		 */
		if (nacquire) {
			tprintf("lcc: %s-%llu: wake waiting thread\n", id.c_str(), lid);
			pthread_cond_signal(&client_wait);
		}
	} else {
		tprintf("lcc: %s-%llu: release later\n", id.c_str(), lid);
		to_release = true;
	}

	tprintf("lcc: %s-%llu: finish revoke_handler\n", id.c_str(), lid);
	pthread_mutex_unlock(&client_lock);

	return rlock_protocol::OK;
}

rlock_protocol::status
lock_client_cache::retry_handler(lock_protocol::lockid_t lid, 
								 int &)
{
	struct local_lock *llock;
	std::map<lock_protocol::lockid_t,
			 struct local_lock *>::iterator it;
	int ret = rlock_protocol::OK;

	pthread_mutex_lock(&client_lock);
	tprintf("lcc: %s-%llu: start retry_handler\n", id.c_str(), lid);

	it = lock_table->find(lid);
	llock = it->second;

	if (llock->waiting_retry)
		pthread_cond_signal(&llock->retry_wait);
	else
		llock->waiting_retry = true;

	tprintf("lcc: %s-%llu: finish retry_handler\n", id.c_str(), lid);
	pthread_mutex_unlock(&client_lock);

	return ret;
}
