// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include "tprintf.h"


lock_client_cache::lock_client_cache(std::string xdst, 
				     class lock_release_user *_lu)
  : lock_client(xdst), lu(_lu)
{
  rpcs *rlsrpc = new rpcs(0);
  rlsrpc->reg(rlock_protocol::revoke, this, &lock_client_cache::revoke_handler);
  rlsrpc->reg(rlock_protocol::retry, this, &lock_client_cache::retry_handler);

  const char *hname;
  hname = "127.0.0.1";
  std::ostringstream host;
  host << hname << ":" << rlsrpc->port();
  id = host.str();

  // Lab4 code
  tprintf("Create a client: id=%s\n", id.c_str());
  pthread_mutex_init(&client_lock, NULL);
  pthread_cond_init(&client_wait, NULL);
  lock_owner = -1;
  
}

lock_protocol::status
lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
  int ret = lock_protocol::OK;

  tprintf("lcc: acquire\n");

  if (lock_owner < 0) {
	  ret = lock_client::acquire(lid);
	  if (ret == lock_protocol::OK)
		  lock_owner = 0;
  }

  pthread_mutex_lock(&client_lock);

  lock_owner = pthread_self();

  pthread_mutex_unlock(&client_lock);

  return lock_protocol::OK;
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
	tprintf("lcc: release\n");

	pthread_mutex_lock(&client_lock);
	lock_owner = 0;
	pthread_mutex_unlock(&client_lock);
	
  return lock_protocol::OK;

}

rlock_protocol::status
lock_client_cache::revoke_handler(lock_protocol::lockid_t lid, 
                                  int &)
{
  int ret = rlock_protocol::OK;
  pthread_mutex_lock(&client_lock);

  if (lock_owner > 0) {
	  // wait for thread to release the lock
  } else
	  lock_owner = -1;
  pthread_mutex_unlock(&client_lock);
  return ret;
}

rlock_protocol::status
lock_client_cache::retry_handler(lock_protocol::lockid_t lid, 
                                 int &)
{
  int ret = rlock_protocol::OK;
  return ret;
}



