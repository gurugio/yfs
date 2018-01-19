#ifndef lock_server_cache_h
#define lock_server_cache_h

#include <string>

#include <map>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_server.h"

class lock_server_cache {
private:
	struct local_lock {
		int status;
		std::string owner;
		std::set<std::string> wait_list;
	};

	int nacquire;
	std::map<lock_protocol::lockid_t, struct local_lock *> *lock_table;
	pthread_mutex_t server_lock;
	pthread_cond_t  server_wait;
public:
	lock_server_cache();
	lock_protocol::status stat(lock_protocol::lockid_t, int &);
	int acquire(lock_protocol::lockid_t, std::string id, int &);
	int release(lock_protocol::lockid_t, std::string id, int &);
};

#endif
