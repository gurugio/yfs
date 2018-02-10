// extent client interface.

#ifndef extent_client_h
#define extent_client_h

#include <string>
#include "extent_protocol.h"
#include "rpc.h"
#include "lock_client_cache.h"

class extent_client : public lock_release_user {
 private:
  rpcc *cl;
  enum cachestatus { CLEAN, DIRTY, REMOVED };

  struct filecache {
	  extent_protocol::attr attr;
	  std::string buf;
	  int status;
  };
  // TODO: add pthread_mutex for filecache
  std::map<extent_protocol::extentid_t, struct filecache *> *filecache_table;

 public:
  extent_client(std::string dst);

  extent_protocol::status get(extent_protocol::extentid_t eid, 
			      std::string &buf);
  extent_protocol::status getattr(extent_protocol::extentid_t eid, 
				  extent_protocol::attr &a);
  extent_protocol::status put(extent_protocol::extentid_t eid, std::string buf);
  extent_protocol::status remove(extent_protocol::extentid_t eid);

  extent_protocol::status flush(extent_protocol::extentid_t);
  void dorelease(extent_protocol::extentid_t);
};

#endif 

