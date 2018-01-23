// lock client interface.

#ifndef lock_client_cache_h

#define lock_client_cache_h

#include <string>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_client.h"
#include "lang/verify.h"

// Classes that inherit lock_release_user can override dorelease so that 
// that they will be called when lock_client releases a lock.
// You will not need to do anything with this class until Lab 5.
class lock_release_user {
 public:
  virtual void dorelease(lock_protocol::lockid_t) = 0;
  virtual ~lock_release_user() {};
};

class lock_client_cache : public lock_client {
 private:
	struct local_lock {
		int status;
		std::string owner;
		// why list?
		// One client can have multiple threads
		// and each thread can send acquire.
		// List can keep the order of requests and save multiple requests.
		std::list<std::string> wait_list;
		pthread_mutex_t retry_lock;
		pthread_cond_t  retry_wait;
	};
	std::map<lock_protocol::lockid_t, struct local_lock *> *lock_table;

	class lock_release_user *lu;
	int rlock_port;
	std::string hostname;
	std::string id;
	pthread_t lock_owner;
	pthread_mutex_t client_lock;
	pthread_cond_t  client_wait;
	enum lock_status_values { LOCK_NONE, LOCK_FREE,
				  LOCK_LOCKED, LOCK_ACQUIRING, LOCK_RELEASING };
	bool to_release;

 public:
  lock_client_cache(std::string xdst, class lock_release_user *l = 0);
  virtual ~lock_client_cache() {};
  lock_protocol::status acquire(lock_protocol::lockid_t);
  lock_protocol::status release(lock_protocol::lockid_t);
  rlock_protocol::status revoke_handler(lock_protocol::lockid_t, 
                                        int &);
  rlock_protocol::status retry_handler(lock_protocol::lockid_t, 
                                       int &);
};


#endif
