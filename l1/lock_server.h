// this is the lock server
// the lock client has a similar interface

#ifndef lock_server_h
#define lock_server_h

#include <string>
#include <map>
#include <iterator>

#include "lock_protocol.h"
#include "lock_client.h"
#include "rpc.h"

struct lock_info {
	pthread_mutex_t lock;
	int locked;
	int id; // client id
};

class lock_server {

 protected:
  int nacquire;
  std::map<std::string, struct lock_info *> *lock_of_names;
  pthread_mutex_t map_lock;

 public:
  lock_server();
  virtual ~lock_server() {};
  virtual lock_protocol::status stat(std::string, int &);
  virtual lock_protocol::status acquire(std::string, const int, int &);
  virtual lock_protocol::status release(std::string, const int, int &);
};



#endif 







