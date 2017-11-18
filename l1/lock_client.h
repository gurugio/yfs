// lock client interface.

#ifndef lock_client_h
#define lock_client_h

#include <string>
#include "lock_protocol.h"
#include "rpc.h"

// Client interface to the lock server
class lock_client {

 protected:
  struct sockaddr_in dst;
  rpcc cl;
  int id;
 public:
  lock_client(struct sockaddr_in dst);
  virtual ~lock_client() {};
  virtual lock_protocol::status acquire(std::string);
  virtual lock_protocol::status release(std::string);
  virtual lock_protocol::status stat(std::string);
};




#endif 
