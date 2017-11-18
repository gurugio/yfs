// RPC stubs for clients to talk to lock_server
#include <stdlib.h>
#include <stdio.h>

#include "lock_client.h"
#include "rpc.h"

lock_client::lock_client(sockaddr_in xdst)
  : dst(xdst)
{
  id = random();
}

int
lock_client::stat(std::string name)
{
	int r;
	printf("client-%d stat: lock-%s\n", id, name.c_str());
  int ret = cl.call(dst, lock_protocol::stat, name, r);
  assert (ret == lock_protocol::OK);
  return r;
}

int
lock_client::acquire(std::string name)
{
	int r;
	printf("client-%d try acquire: lock-%s\n", id, name.c_str());
	int ret = cl.call(dst, lock_protocol::acquire, name, id, r);
	if (r == 0) printf("### client-%d succeed lock-%s\n", id, name.c_str());
	if (r < 0) printf("###### client-%d failed lock-%s err=%d\n",
			  id, name.c_str(), r);

	assert (ret == lock_protocol::OK);
	return 1;
}

int
lock_client::release(std::string name)
{
	int r;
	printf("client-%d release: lock-%s\n", id, name.c_str());
	int ret = cl.call(dst, lock_protocol::release, name, id, r);
	assert (ret == lock_protocol::OK);
	if (r == 0) printf("###client-%d succeed unlock-%s\n",
			   id, name.c_str());
	if (r < 0) printf("###### client-%d failed unlock-%s\n",
			  id, name.c_str());
	return 1;
}

