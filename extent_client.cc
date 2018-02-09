// RPC stubs for clients to talk to extent_server

#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

// The calls assume that the caller holds a lock on the extent

extent_client::extent_client(std::string dst)
{
  sockaddr_in dstsock;

  // TODO: create a cache for attr and file-buffer
  filecache_table = new std::map<extent_protocol::extentid_t, struct filecache *>;

  make_sockaddr(dst.c_str(), &dstsock);
  cl = new rpcc(dstsock);
  if (cl->bind() != 0) {
    printf("extent_client: bind failed\n");
  }
}

extent_protocol::status
extent_client::get(extent_protocol::extentid_t eid, std::string &buf)
{
  extent_protocol::status ret = extent_protocol::OK;
  struct filecache *fcache;
  std::map<extent_protocol::extentid_t, struct filecache *>::iterator it;

  it = filecache_table->find(eid);
  if (it == filecache_table->end()) {
	  fcache = new filecache;
	  ret = cl->call(extent_protocol::get, eid, fcache->buf);
	  if (ret != extent_protocol::OK) {
		  delete fcache;
		  goto rpc_error;
	  }

	  ret = cl->call(extent_protocol::getattr, eid, fcache->attr);
	  if (ret != extent_protocol::OK) {
		  delete fcache;
		  goto rpc_error;
	  }

	  filecache_table->insert(std::make_pair(eid, fcache));
  }

  it = filecache_table->find(eid);
  fcache = it->second;
  buf = fcache->buf;
  fcache->dirty = false;

rpc_error:
  return ret;
}

extent_protocol::status
extent_client::getattr(extent_protocol::extentid_t eid, 
		       extent_protocol::attr &attr)
{
  extent_protocol::status ret = extent_protocol::OK;
  struct filecache *fcache;
  std::map<extent_protocol::extentid_t, struct filecache *>::iterator it;

  it = filecache_table->find(eid);
  if (it == filecache_table->end()) {
	  return extent_protocol::NOENT;
  }

  fcache = it->second;
  attr = fcache->attr;
  // step 1
  //ret = cl->call(extent_protocol::getattr, eid, attr);
  return ret;
}

extent_protocol::status
extent_client::put(extent_protocol::extentid_t eid, std::string buf)
{
  extent_protocol::status ret = extent_protocol::OK;
  std::map<extent_protocol::extentid_t, struct filecache *>::iterator it;
  //int r; step 1
  struct filecache *fcache;

  it = filecache_table->find(eid);
  if (it == filecache_table->end()) {
	  printf("ec: put: create cache for %016llx\n", eid);
	  fcache = new filecache;
	  fcache->buf = buf;
	  fcache->attr.mtime = fcache->attr.ctime = fcache->attr.atime = time(NULL);
	  fcache->attr.size = 0;
	  filecache_table->insert(std::make_pair(eid, fcache));
  } else {
	  printf("ec: put: change cache for %016llx\n", eid);
	  fcache = it->second;
	  fcache->buf = buf;
	  fcache->attr.mtime = fcache->attr.ctime = time(NULL);
	  fcache->attr.size = buf.length();
  }
  // put always makes filecache dirty
  fcache->dirty = true;

  // step 1
  //ret = cl->call(extent_protocol::put, eid, buf, r);
  return ret;
}

extent_protocol::status
extent_client::remove(extent_protocol::extentid_t eid)
{
  extent_protocol::status ret = extent_protocol::OK;
  std::map<extent_protocol::extentid_t, struct filecache *>::iterator it;
  int r;
  struct filecache *fcache;
  extent_protocol::attr attr;

  it = filecache_table->find(eid);
  if (it == filecache_table->end()) {
	  return extent_protocol::NOENT;
  }

  it = filecache_table->find(eid);
  fcache = it->second;
  filecache_table->erase(it);
  delete fcache;

  // server data also should be removed if server has data
  ret = cl->call(extent_protocol::getattr, eid, attr);
  if (ret == extent_protocol::OK)
	  ret = cl->call(extent_protocol::remove, eid, r);
  else
	  ret = extent_protocol::OK; // no need to call remove-rpc

  return ret;
}

void extent_client::flush(extent_protocol::extentid_t eid)
{
  printf("ec: flush: flush cache of %016llx\n", eid);

#if 0
  std::map<extent_protocol::extentid_t, struct filecache *>::iterator it;
  int r;
  struct filecache *fcache;

  it = filecache_table->find(eid);
  if (it == filecache_table->end()) {
	  return;
  }

  fcache = it->second;

  // put-rpc only-if cache is dirty
  if (fcache->dirty) {
	  // do not care error
	  cl->call(extent_protocol::put, eid, fcache->buf, r);
  }
  filecache_table->erase(it);
  delete fcache;
#endif
}
