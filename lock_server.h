// this is the lock server
// the lock client has a similar interface

#ifndef lock_server_h
#define lock_server_h

#include <string>
#include <iostream>
#include <set>

#include "lock_protocol.h"
#include "lock_client.h"
#include "rpc.h"

struct local_lock {
        /* pthread_mutex_t lock; */
        /* pthread_cond_t wait_cond; */
        int status;
        int owner;
        /* std::string name; */
        /* std::set<int> wait_list; */
};

class lock_server {
protected:
	int nacquire;
	std::map<lock_protocol::lockid_t, struct local_lock *> *lock_table;
	pthread_mutex_t server_lock;
	pthread_cond_t  server_wait;
public:
	lock_server();
	~lock_server() {};
	lock_protocol::status stat(int clt,
				   lock_protocol::lockid_t lid,
				   int &);
	lock_protocol::status acquire(int clt,
				      lock_protocol::lockid_t lid,
				      int &);
	lock_protocol::status release(int clt,
				      lock_protocol::lockid_t lid,
				      int &);
};

#endif 







