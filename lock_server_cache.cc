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
}


int lock_server_cache::acquire(lock_protocol::lockid_t lid, std::string id, 
                               int &)
{
    lock_protocol::status ret = lock_protocol::OK;
    fprintf(stdout, "ls: %s-%llu: start acquire\n", id.c_str(), lid);


    fprintf(stdout, "ls: %s-%llu: finish acquire\n", id.c_str(), lid);
    return ret;
}

int 
lock_server_cache::release(lock_protocol::lockid_t lid, std::string id, 
         int &r)
{
    lock_protocol::status ret = lock_protocol::OK;
    tprintf("ls: %s-%llu: start release\n", id.c_str(), lid);
    tprintf("ls: %s-%llu: finish release\n", id.c_str(), lid);
    return ret;
}

lock_protocol::status
lock_server_cache::stat(lock_protocol::lockid_t lid, int &r)
{
    tprintf("stat request: %llu\n", lid);
  r = nacquire;
  return lock_protocol::OK;
}

