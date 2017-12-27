// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define INUM_MASK 0x00000000ffffffffULL

yfs_client::inum yfs_client::get_nextid(int isfile)
{
	if (isfile)
		return (inum)(random() & INUM_MASK) | 0x80000000;
	else
		return (inum)(random() & INUM_MASK) & ~0x80000000;
}

yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
	ec = new extent_client(extent_dst);
}

yfs_client::inum
yfs_client::n2i(std::string n)
{
  std::istringstream ist(n);
  unsigned long long finum;
  ist >> finum;
  return finum;
}

std::string
yfs_client::filename(inum inum)
{
  std::ostringstream ost;
  ost << inum;
  return ost.str();
}

bool
yfs_client::isfile(inum inum)
{
  if(inum & 0x80000000)
    return true;
  return false;
}

bool
yfs_client::isdir(inum inum)
{
  return ! isfile(inum);
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
  int r = OK;

  printf("yfs:getfile: inum=%016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
	  DPRINTF("yfs:getfile: fail\n");
    r = IOERR;
    goto release;
  }

  fin.atime = a.atime;
  fin.mtime = a.mtime;
  fin.ctime = a.ctime;
  fin.size = a.size;
  DPRINTF("yfs:getfile: %016llx -> sz %llu\n", inum, fin.size);

 release:

  return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  int r = OK;

  printf("yfs:getdir: inum=%016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
	  DPRINTF("yfs:getfile: fail\n");
    r = IOERR;
    goto release;
  }
  din.atime = a.atime;
  din.mtime = a.mtime;
  din.ctime = a.ctime;
  DPRINTF("yfs:getdir: %lu %lu %lu\n", din.atime, din.mtime, din.ctime);
 release:
  return r;
}

int yfs_client::add_dirent(inum dir_inum, const char *name, inum file_inum)
{
	std::string buf;
	int r = OK;
	char inum_buf[17]; // 16-digit

	if (ec->get(dir_inum, buf) != extent_protocol::OK)
		return IOERR;

	// add inum and name
	sprintf(inum_buf, "%016llx", file_inum);
	inum_buf[16] = '\0';
	printf("yfs:add_dirent: inum_buf=%s\n", inum_buf);
	buf.append(inum_buf);
	buf.push_back('\0');
	buf.append(name);
	buf.push_back('\0');

#if DEBUG
		const char *ptr = buf.c_str();
		printf("dir-inum=%x buf=", (int)dir_inum);
		for (unsigned int i = 0; i < buf.length(); i++) {
			if (ptr[i] == 0)
				printf("#");
			else
				printf("%c", ptr[i]);
		}
		printf("\n");
#endif
	
	if (ec->put(dir_inum, buf) != extent_protocol::OK)
		return IOERR;
	DPRINTF("yfs:add_dirent: ret=%d\n", r);
	return r;
}

int yfs_client::createfile(inum parent_inum,
						   const char *name, inum *file_inum)
{
	int r = OK;
	std::string filename;
	DPRINTF("yfs: create: %016llx %s\n", parent_inum, name);
	inum finum = get_nextid(1);

	printf("yfs:create: new fileinum=%016llx\n", finum);
	if (add_dirent(parent_inum, name, finum) != extent_protocol::OK) {
		DPRINTF("yfs:create: fail to add dirent\n");
		return IOERR;
	}

	(*file_inum) = finum;

	filename = name;
	if (ec->put(finum, filename) != extent_protocol::OK) {
		DPRINTF("yfs:create: fail to create file\n");
		return IOERR;
	}

	DPRINTF("yfs: create: exit with %d\n", r);
	return r;
}

int yfs_client::createdir(inum parent_inum,
						   const char *name, inum *dir_inum)
{
	int r = OK;
	std::string dirname;
	DPRINTF("yfs: createdir: %016llx %s\n", parent_inum, name);
	inum finum = get_nextid(0);

	printf("yfs:createdir: new fileinum=%016llx\n", finum);
	if (add_dirent(parent_inum, name, finum) != extent_protocol::OK) {
		DPRINTF("yfs:createdir: fail to add dirent\n");
		return IOERR;
	}

	(*dir_inum) = finum;

	dirname = name;
	if (ec->put(finum, dirname) != extent_protocol::OK) {
		DPRINTF("yfs:create: fail to create file\n");
		return IOERR;
	}

	DPRINTF("yfs: create: exit with %d\n", r);
	return r;
}

int yfs_client::lookup(inum parent_inum, const char *name, inum &file_inum)
{
	std::string dir_buf;
	std::size_t found;

	printf("lookup: parent_inum=%llu name=%s\n", parent_inum, name);

	if (ec->get(parent_inum, dir_buf) != extent_protocol::OK)
		return IOERR;

	found = dir_buf.find(name);
	if (found == std::string::npos) {
		DPRINTF("lookup: NOENT\n");
		return NOENT;
	}else {
		found -= 17;
		DPRINTF("lookup: found inum=%s\n", dir_buf.c_str() + found);
		file_inum = strtoull(dir_buf.c_str() + found, NULL, 16);
		DPRINTF("lookup: file_inum=%016llx\n", file_inum);
	}
	return EXIST;
}

int yfs_client::readdir(inum parent_inum,
						struct dirent **dir_entries,
						int *num_entries)
{
	std::string dir_buf;
	const char *ptr;
	int count = 0;
	struct dirent *entries;

	printf("yfs:readdir: %016llx\n", parent_inum);

	if (ec->get(parent_inum, dir_buf) != extent_protocol::OK)
		return IOERR;

	ptr = dir_buf.c_str();

#if DEBUG
	{
		for (unsigned int i = 0; i < dir_buf.length(); i++) {
			if (ptr[i] == 0)
				printf("#");
			else
				printf("%c", ptr[i]);
		}
		printf("\n");
	}
#endif

	for (unsigned int i = 0; i <= dir_buf.length(); i++) {
		if (ptr[i] == 0)
			count++;
	}

	count /= 2;
	entries = (struct dirent *)calloc(count, sizeof(struct dirent));

	for (int i = 0; i < count; i++) {
		DPRINTF("yfs:readdir: inum=%s\n", ptr);
		entries[i].inum = strtoull(ptr, NULL, 16);
		ptr += (strlen(ptr) + 1);
		DPRINTF("yfs:readdir: name=%s\n", ptr);
		entries[i].name = ptr;
		ptr += (strlen(ptr) + 1);
	}

	(*dir_entries) = entries;
	(*num_entries) = count;
	return OK;
}

int yfs_client::resizefile(inum file_inum, size_t size)
{
	std::string file_buf;

	printf("yfs:resize: inum=%016llx size=%lu\n", file_inum, size);

	if (ec->get(file_inum, file_buf) != extent_protocol::OK)
		return IOERR;

	file_buf.resize(size);

	if (ec->put(file_inum, file_buf) != extent_protocol::OK) {
		return IOERR;
	}

	return OK;
}

int yfs_client::writefile(inum file_inum, const char *buf,
						  size_t size, off_t off)
{
	std::string file_buf;
	char *newbuf;
	int newsize;

	printf("yfs:writefile: inum=%016llx size=%lu off=%lu\n",
		   file_inum, size, off);

	if (ec->get(file_inum, file_buf) != extent_protocol::OK)
		return -1;

	DPRINTF("yfs:writefile: orig-len=%d\n",
		   (int)file_buf.length());

	if (file_buf.length() < (size + off))
		newsize = size + off;
	else
		newsize = file_buf.length();

	newbuf = (char *)calloc(newsize, sizeof(char));
	memcpy(newbuf, file_buf.c_str(), file_buf.length());
	memcpy(&newbuf[off], buf, size);
	file_buf = std::string(newbuf, newsize);

	DPRINTF("yfs:writefile: new-len=%d\n",
			(int)file_buf.length());

	if (ec->put(file_inum, file_buf) != extent_protocol::OK)
		return -1;

	free(newbuf);
	return size;
}

int yfs_client::readfile(inum file_inum, size_t size,
						 off_t off, std::string &buf)
{
	std::string file_buf;
	int readsize;

	printf("yfs:readfile: inum=%016llx size=%lu off=%lu\n",
		   file_inum, size, off);

	if (ec->get(file_inum, file_buf) != extent_protocol::OK)
		return -1;

	if (off > (int)file_buf.length())
		readsize = 0;
	else if (file_buf.length() < off + size)
		readsize = file_buf.length() - off;
	else
		readsize = size;

	DPRINTF("yfs:readfile: readsize=%d\n", readsize);
	buf = std::string(file_buf.c_str() + off, readsize);

	return readsize;
}

int yfs_client::unlink(inum parent_inum, inum file_inum)
{
	int ret = OK;
	std::string buf;
	char inum_buf[17]; // 16-digit

	if (ec->get(parent_inum, buf) != extent_protocol::OK)
		return IOERR;

	sprintf(inum_buf, "%016llx", file_inum);
	inum_buf[16] = '\0';
	printf("yfs:unlink file: inumbuf=%016llx inum=%s\n",
		   inum_buf, );


	// find inum_buf in buf

	// remove inum_buf and filename in buf
	
	return ret;
}
