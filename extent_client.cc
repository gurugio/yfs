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

  // todo: add pthread_mutex to protect filecache_table
  filecache_table = new std::map<extent_protocol::extentid_t, struct filecache *>;
  pthread_mutex_init(&filecache_lock, NULL);

  make_sockaddr(dst.c_str(), &dstsock);
  cl = new rpcc(dstsock);
  if (cl->bind() != 0) {
    printf("extent_client: bind failed\n");
  }
}

extent_client::filecache *
extent_client::create_filecache(extent_protocol::extentid_t eid)
{
	extent_protocol::status ret = extent_protocol::OK;
	struct filecache *fcache;
	std::map<extent_protocol::extentid_t, struct filecache *>::iterator it;

	fcache = new filecache;

	ret = cl->call(extent_protocol::get, eid, fcache->buf);
	if (ret == extent_protocol::OK) {
		// file exists in extent server
		printf("ec: create: get from server eid=%016llX\n", eid);
		ret = cl->call(extent_protocol::getattr, eid, fcache->attr);
		if (ret != extent_protocol::OK)
			goto rpc_error;
	} else {
		// cannot create file-cache if file does not exist
		goto rpc_error;
	}

	fcache->status = extent_client::CLEAN;
	return fcache;
rpc_error:
	delete fcache;
	return NULL;
}

extent_protocol::status
extent_client::get(extent_protocol::extentid_t eid, std::string &buf)
{
  extent_protocol::status ret = extent_protocol::OK;
  struct filecache *fcache;
  std::map<extent_protocol::extentid_t, struct filecache *>::iterator it;

  printf("ec: get: eid=%016llX\n", eid);

  pthread_mutex_lock(&filecache_lock);

  it = filecache_table->find(eid);
  if (it == filecache_table->end()) {
	  pthread_mutex_unlock(&filecache_lock);
	  fcache = create_filecache(eid);
	  if (!fcache) {
		  ret = extent_protocol::NOENT;
		  goto create_error;
	  }
	  pthread_mutex_lock(&filecache_lock);
	  filecache_table->insert(std::make_pair(eid, fcache));
  } else {
	  it = filecache_table->find(eid);
	  fcache = it->second;
	  printf("ec: get: found cache: size=%d buf-len=%d\n",
			 (int)fcache->attr.size,
			 (int)fcache->buf.size());
  }

  if (fcache->status == REMOVED) {
	  printf("ec: get: removed eid=%016llX\n", eid);
	  ret = extent_protocol::NOENT;
	  goto remove_error;
  }

  buf = fcache->buf;

remove_error:
  pthread_mutex_unlock(&filecache_lock);
create_error:
  return ret;
}

extent_protocol::status
extent_client::getattr(extent_protocol::extentid_t eid, 
		       extent_protocol::attr &attr)
{
  extent_protocol::status ret = extent_protocol::OK;
  struct filecache *fcache;
  std::map<extent_protocol::extentid_t, struct filecache *>::iterator it;

  printf("ec: getattr: eid=%016llX\n", eid);

  pthread_mutex_lock(&filecache_lock);

  it = filecache_table->find(eid);
  if (it == filecache_table->end()) {
	  pthread_mutex_unlock(&filecache_lock);
	  fcache = create_filecache(eid);
	  if (!fcache) {
		  ret = extent_protocol::NOENT;
		  goto rpc_error;
	  }
	  pthread_mutex_lock(&filecache_lock);
	  filecache_table->insert(std::make_pair(eid, fcache));
  } else {
	  it = filecache_table->find(eid);
	  fcache = it->second;
  }

  if (fcache->status == REMOVED) {
	  printf("ec: get: removed eid=%016llX\n", eid);
	  ret = extent_protocol::NOENT;
	  goto rpc_error;
  }

  attr = fcache->attr;

rpc_error:
  pthread_mutex_unlock(&filecache_lock);
  return ret;
}

extent_protocol::status
extent_client::put(extent_protocol::extentid_t eid, std::string buf)
{
  extent_protocol::status ret = extent_protocol::OK;
  std::map<extent_protocol::extentid_t, struct filecache *>::iterator it;
  //int r; step 1
  struct filecache *fcache;

  pthread_mutex_lock(&filecache_lock);

  it = filecache_table->find(eid);
  if (it == filecache_table->end()) {
	  // no need to get data from extent server
	  // because it will be over-written later
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
	  // if a file/dir is removed and the same i-number file/dir is created,
	  // atime also should be changed.
	  if (fcache->status == REMOVED) {
		  printf("ec: put: chage removed cache\n");
		  fcache->attr.atime = time(NULL);
	  }
	  fcache->attr.size = buf.length();
  }
  // put always makes filecache dirty
  fcache->status = extent_client::DIRTY;
  pthread_mutex_unlock(&filecache_lock);
  return ret;
}

extent_protocol::status
extent_client::remove(extent_protocol::extentid_t eid)
{
  std::map<extent_protocol::extentid_t, struct filecache *>::iterator it;
  struct filecache *fcache;

  pthread_mutex_lock(&filecache_lock);

  it = filecache_table->find(eid);
  if (it == filecache_table->end()) {
	  pthread_mutex_unlock(&filecache_lock);
	  return extent_protocol::OK; // already removed?
  }

  fcache = it->second;
  // what happens if removed cache is created or accessed?
  fcache->status = extent_client::REMOVED;
  fcache->buf = "";

  pthread_mutex_unlock(&filecache_lock);
  return extent_protocol::OK;
}

extent_protocol::status
extent_client::flush(extent_protocol::extentid_t eid,
					 struct filecache *fcache)
{
  extent_protocol::status ret = extent_protocol::OK;
  int r;

  printf("ec: flush: flush cache of %016llx\n", eid);

  if (fcache->status == extent_client::DIRTY) {
	  printf("ec: flush: flush dirty cache of %016llx\n", eid);
	  ret = cl->call(extent_protocol::put, eid, fcache->buf, r);
	  if (ret != extent_protocol::OK)
		  goto rpc_error; // BUGBUG: exit or ignore?
  } else if (fcache->status == extent_client::REMOVED) {
	  printf("ec: flush: remove extent in server of %016llx\n", eid);
	  ret = cl->call(extent_protocol::remove, eid, r);
	  if (ret != extent_protocol::OK)
		  goto rpc_error; // BUGBUG: exit or ignore?
  }

rpc_error:
  return ret;
}

void extent_client::dorelease(extent_protocol::extentid_t eid)
{
  std::map<extent_protocol::extentid_t, struct filecache *>::iterator it;
  struct filecache *fcache;

  printf("ec: [ dorelease ]: flush and remove cache of %016llx\n", eid);

  pthread_mutex_lock(&filecache_lock);
  it = filecache_table->find(eid);
  if (it == filecache_table->end()) {
	  pthread_mutex_unlock(&filecache_lock);
	  return;
  }

  fcache = it->second;
  filecache_table->erase(it);
  pthread_mutex_unlock(&filecache_lock);

  (void)flush(eid, fcache);
  delete fcache;
}
