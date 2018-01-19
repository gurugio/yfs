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
  pthread_mutex_init(&retry_lock, NULL);
  pthread_cond_init(&retry_wait, NULL);
  lock_owner = 0;
  lock_status = LOCK_NONE;
}

lock_protocol::status
lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
  int ret = lock_protocol::OK;

  tprintf("lcc: %s-%llu: start acquire\n", id.c_str(), lid);

  pthread_mutex_lock(&client_lock);

  while (lock_status == LOCK_LOCKED
	 || lock_status == LOCK_ACQUIRING
	 || lock_status == LOCK_RELEASING) {
      pthread_cond_wait(&client_wait, &client_lock);
  }
  
  if (lock_status == LOCK_NONE) {
      lock_status = LOCK_ACQUIRING;
      tprintf("lcc: %s-%llu: call RPC-acquire\n", id.c_str(), lid);
      // MUST unlock before RPC call
      pthread_mutex_unlock(&client_lock);

      //ret = lock_client::acquire(id, lid);
      ret = cl->call(lock_protocol::acquire_cache, lid, id);
      if (ret == lock_protocol::OK) {
	  lock_owner = pthread_self();
	  tprintf("lcc: %s-%llu: RPC-OK\n", id.c_str(), lid);
      } else if (ret == lock_protocol::RETRY) {
	  tprintf("lcc: %s-%llu: RPC-RETRY\n", id.c_str(), lid);
	  // wait server sends retry rpc and retry_handler is called
	  pthread_mutex_lock(&retry_lock);
	  pthread_cond_wait(&retry_wait, &retry_lock);
	  pthread_mutex_unlock(&retry_lock);
      } else {
	  return lock_protocol::RPCERR;
      }

      pthread_mutex_lock(&client_lock);
      lock_status = LOCK_LOCKED;
  } else if (lock_status == LOCK_FREE) {
      lock_owner = pthread_self();
      lock_status = LOCK_LOCKED;
  }

  tprintf("lcc: %s-%llu: finish acquire\n", id.c_str(), lid);
  pthread_mutex_unlock(&client_lock);

  return lock_protocol::OK;
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
    tprintf("lcc: %s-%llu: start release\n", id.c_str(), lid);

    pthread_mutex_lock(&client_lock);
    lock_owner = 0;
    lock_status = LOCK_FREE;
    pthread_cond_signal(&client_wait);
    pthread_mutex_unlock(&client_lock);

    tprintf("lcc: %s-%llu: finish release\n", id.c_str(), lid);
	
    return lock_protocol::OK;
}

rlock_protocol::status
lock_client_cache::revoke_handler(lock_protocol::lockid_t lid, 
                                  int &)
{
  int ret = rlock_protocol::OK;

  tprintf("lcc: %s-%llu: start revoke\n", id.c_str(), lid);

  pthread_mutex_lock(&client_lock);

  if (lock_status == LOCK_LOCKED
      || lock_status == LOCK_ACQUIRING
      || lock_status == LOCK_RELEASING) {
	  // wait for thread to release the lock

  }

  if (lock_status == LOCK_FREE) {
      lock_status = LOCK_RELEASING;
      pthread_mutex_unlock(&client_lock);
      
      //ret = lock_client::release(id, lid);
      ret = cl->call(lock_protocol::release_cache, lid, id);
      if (ret == lock_protocol::OK) {
	  lock_status = LOCK_NONE;
      } else {
	  return rlock_protocol::RPCERR;
      }	  
      tprintf("lcc: %s-%llu: call RPC-release\n", id.c_str(), lid);
  } else {
      // LOCK_NONE
      // ERROR: not my lock
  }

  pthread_mutex_unlock(&client_lock);

  tprintf("lcc: %s-%llu: finish revoke\n", id.c_str(), lid);

  return ret;
}

rlock_protocol::status
lock_client_cache::retry_handler(lock_protocol::lockid_t lid, 
                                 int &)
{
    int ret = rlock_protocol::OK;

    tprintf("lcc: %s-%llu: start retry\n", id.c_str(), lid);

    pthread_mutex_lock(&retry_lock);
    pthread_cond_signal(&retry_wait);
    pthread_mutex_unlock(&retry_lock);

    tprintf("lcc: %s-%llu: finish retry\n", id.c_str(), lid);

    return ret;
}



