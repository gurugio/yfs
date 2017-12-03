#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "extent_server.h"

// $ g++ unit_extent_server.cc extent_server.cc -I./rpc/ -I.

int main(int argc, char *argv[])
{
	extent_server serv;
	extent_protocol::extentid_t id1 = 111;
	extent_protocol::extentid_t id2 = 222;
	extent_protocol::extentid_t id3 = 333;

	std::string name1 = "name111";
	std::string name2 = "name222";
	std::string name3 = "name333";
	std::string buf;
	extent_protocol::attr a;

	int dummy;
	int ret;

	printf("time=%d\n", (int)time(NULL));
	serv.put(id1, name1, dummy);
	serv.put(id2, name2, dummy);
	serv.put(id2, name2, dummy);
	
	ret = serv.get(id1, buf);
	printf("ret=%d name1=%s\n", ret, buf.c_str());
	serv.getattr(id1, a);
	printf("%d %d %d %d\n", a.size, a.atime, a.mtime, a.ctime);

	ret = serv.get(id2, buf);
	printf("ret=%d name2=%s\n", ret, buf.c_str());
	serv.getattr(id2, a);
	printf("%d %d %d %d\n", a.size, a.atime, a.mtime, a.ctime);

	ret = serv.get(id3, buf);
	printf("ret=%d name3=%s\n", ret, buf.c_str());
	serv.getattr(id3, a);
	printf("%d %d %d %d\n", a.size, a.atime, a.mtime, a.ctime);

	buf = "";
	ret = serv.remove(id3, dummy);
	ret = serv.get(id3, buf);
	printf("ret=%d name3=%s\n", ret, buf.c_str());

	sleep(2);
	serv.put(id3, name1+name3, dummy);
	ret = serv.get(id3, buf);
	printf("ret=%d name3=%s\n", ret, buf.c_str());
	serv.getattr(id3, a);
	printf("%d %d %d %d\n", a.size, a.atime, a.mtime, a.ctime);
	
	return 0;
}
