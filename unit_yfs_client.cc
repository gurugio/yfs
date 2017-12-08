#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lang/verify.h"
#include "yfs_client.h"

// build command
// $ g++ -g -MMD -Wall -I. -I./rpc -DLAB=2 -DSOL=0 -D_FILE_OFFSET_BITS=64 -D_FILE_OFFSET_BITS=64 yfs_client.cc unit_yfs_client.cc extent_client.cc rpc/librpc.a  -lpthread
// $ ./a.out 

yfs_client *yfs;

int main(void)
{
	unsigned long long root_inum = 1;
	unsigned long long file1_inum = 0x80000002, file2_inum=0x80000003;
	int ret;
		
	yfs = new yfs_client("0", "0"); // fake port numbers

	ret = yfs->isdir(root_inum);
	printf("isdir=%d\n", ret);
	ret = yfs->isfile(file1_inum);
	printf("isfile=%d\n", ret);
	ret = yfs->isfile(file2_inum);
	printf("isfile=%d\n", ret);
	
	ret = yfs->createfile(root_inum, file1_inum, "file1", 0);
	printf("createfile=%d\n", ret);
	ret = yfs->createfile(root_inum, file2_inum, "file2", 0);
	printf("createfile=%d\n", ret);

	// yfs->getdir, yfs->getfile

	return 0;
}
