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

#undef DEBUG
#define DEBUG 1

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
	lc = new lock_client_cache(lock_dst, ec);
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
  lc->acquire(inum);

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
 release:
  lc->release(inum);
  printf("yfs:getfile: %016llx -> sz %llu\n", inum, fin.size);
  return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  int r = OK;

  printf("yfs:getdir: inum=%016llx\n", inum);
  lc->acquire(inum);

  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
	  DPRINTF("yfs:getfile: fail\n");
    r = IOERR;
    goto release;
  }
  din.atime = a.atime;
  din.mtime = a.mtime;
  din.ctime = a.ctime;
release:
  lc->release(inum);
  printf("yfs:getdir: %lu %lu %lu\n", din.atime, din.mtime, din.ctime);
  return r;
}

int yfs_client::__add_dirent(inum dir_inum, const char *name, inum file_inum)
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
	inum finum;
	DPRINTF("yfs: createfile: parent=%016llx file=%s\n", parent_inum, name);

	lc->acquire(parent_inum);

	r = __lookup(parent_inum, name, finum);
	if (r == EXIST) {
		printf("yfs:createfile: duplicated file name\n");
		goto release;
	}

	// reset return value
	r = OK;
	finum = get_nextid(1);
	printf("yfs:createfile: new file-inum=%016llx\n", finum);

	if (__add_dirent(parent_inum, name, finum) != extent_protocol::OK) {
		DPRINTF("yfs:createfile: fail to add dirent\n");
		r = IOERR;
		goto release;
	}

	(*file_inum) = finum;
	filename = name;
	if (ec->put(finum, filename) != extent_protocol::OK) {
		DPRINTF("yfs:createfile: fail to create file\n");
		r = IOERR;
		goto release;
	}
release:
	lc->release(parent_inum);
	printf("yfs:createfile: exit with %d\n", r);
	return r;
}

int yfs_client::createdir(inum parent_inum,
						   const char *name, inum *dir_inum)
{
	int r = OK;
	std::string dirname;
	inum finum;

	DPRINTF("yfs: createdir: %016llx %s\n", parent_inum, name);
	lc->acquire(parent_inum);

	r = __lookup(parent_inum, name, finum);
	if (r == EXIST) {
		printf("yfs:createdir: duplicated dir name\n");
		goto release;
	}

	// reset return value
	r = OK;
	finum = get_nextid(0);
	printf("yfs:createdir: new fileinum=%016llx\n", finum);

	if (__add_dirent(parent_inum, name, finum) != extent_protocol::OK) {
		DPRINTF("yfs:createdir: fail to add dirent\n");
		r = IOERR;
		goto release;
	}

	(*dir_inum) = finum;
	dirname = name;
	if (ec->put(finum, dirname) != extent_protocol::OK) {
		DPRINTF("yfs:create: fail to create file\n");
		r = IOERR;
		goto release;
	}

release:
	lc->release(parent_inum);
	printf("yfs:createdir: exit with %d\n", r);
	return r;
}

int yfs_client::__lookup(inum parent_inum, const char *name, inum &file_inum)
{
	std::string dir_buf;
	std::size_t found;
	std::size_t len = strlen(name);
	int r = EXIST;

	if (ec->get(parent_inum, dir_buf) != extent_protocol::OK) {
		r = IOERR;
		goto release;
	}

	/*
	 * For example, the file name with "f3" can have inum 0xf3f3f3f3.
	 * Because inum is stored as string, searching "f3" will return
	 * the index of inum string, not name data.
	 * For that case, it is necessary to check the found substring
	 * is actual name. inum string always starts with "00000000",
	 * for instance 00000000f3f3f3f3. We can be sure if [found-1] is NULL.
	 * If [found-1] is not NULL, restart searching from [found+1].
	 * Another solution is using strtok() and getting token of every
	 * (inum,filename) pairs. Then we can compare only filenames.
	 */
	found = -1;
	do {
		found++;
		found = dir_buf.find(name, found);
		if (found == std::string::npos)
			break;
#if DEBUG
		if (dir_buf[found - 1] != 0 || dir_buf[found + len] != 0)
			printf("\n\n############# inum conflicts name\n\n\n");
#endif
	} while (dir_buf[found - 1] != 0 || dir_buf[found + len] != 0);

	if (found == std::string::npos) {
		DPRINTF("lookup: NOENT found=%d\n", (int)found);
		r = NOENT;
		goto release;
	}

	found -= 17;
	DPRINTF("yfs:lookup: found inum=%s\n", dir_buf.c_str() + found);
	file_inum = strtoull(dir_buf.c_str() + found, NULL, 16);
release:
	return r;
}

int yfs_client::lookup(inum parent_inum, const char *name, inum &file_inum)
{
	int r;
	printf("lookup: parent_inum=%llx name=%s\n", parent_inum, name);
	lc->acquire(parent_inum);
	r = __lookup(parent_inum, name, file_inum);
	lc->release(parent_inum);
	printf("yfs:lookup: found file_inum=%016llx ret=%d\n",
		   file_inum, (int)r);
	return r;
}

int yfs_client::readdir(inum parent_inum,
						struct dirent **dir_entries,
						int *num_entries)
{
	std::string dir_buf;
	const char *ptr;
	int count = 0;
	struct dirent *entries;
	int r = OK;

	printf("yfs:readdir: %016llx\n", parent_inum);
	lc->acquire(parent_inum);

	if (ec->get(parent_inum, dir_buf) != extent_protocol::OK) {
		r = IOERR;
		goto release;
	}

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

release:
	lc->release(parent_inum);
	printf("yfs:readdir: ret=%d\n", r);
	return r;
}

int yfs_client::resizefile(inum file_inum, size_t size)
{
	std::string file_buf;
	extent_protocol::attr a;
	int r = OK;

	printf("yfs:resize: inum=%016llx size=%lu\n", file_inum, size);
	lc->acquire(file_inum);

	if (!isfile(file_inum)) {
		r = NOENT;
		goto release;
	}

	if (ec->getattr(file_inum, a) != extent_protocol::OK) {
		r = NOENT;
		goto release;
	}

	if (ec->get(file_inum, file_buf) != extent_protocol::OK) {
		r = IOERR;
		goto release;
	}

	file_buf.resize(size);

	if (ec->put(file_inum, file_buf) != extent_protocol::OK) {
		r = IOERR;
		goto release;
	}

release:
	lc->release(file_inum);
	printf("yfs:resize: inum=%016llx ret=%d\n", file_inum, r);
	return r;
}

int yfs_client::writefile(inum file_inum, const char *buf,
						  size_t size, off_t off)
{
	std::string file_buf;
	char *newbuf;
	int newsize;

	printf("yfs:writefile: inum=%016llx size=%lu off=%lu\n",
		   file_inum, size, off);
	lc->acquire(file_inum);

	if (ec->get(file_inum, file_buf) != extent_protocol::OK) {
		size = -1;
		goto release;
	}

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
		size = -1;

	free(newbuf);
release:
	lc->release(file_inum);
	printf("yfs:writefile: new-len=%d ret=%d\n",
		   (int)file_buf.length(), (int)size);
	return size;
}

int yfs_client::readfile(inum file_inum, size_t size,
						 off_t off, std::string &buf)
{
	std::string file_buf;
	int readsize;

	printf("yfs:readfile: inum=%016llx size=%lu off=%lu\n",
		   file_inum, size, off);
	lc->acquire(file_inum);

	if (ec->get(file_inum, file_buf) != extent_protocol::OK) {
		readsize = -1;
		goto release;
	}

	if (off > (int)file_buf.length())
		readsize = 0;
	else if (file_buf.length() < off + size)
		readsize = file_buf.length() - off;
	else
		readsize = size;

	DPRINTF("yfs:readfile: readsize=%d\n", readsize);
	buf = std::string(file_buf.c_str() + off, readsize);

release:
	lc->release(file_inum);
	return readsize;
}

int yfs_client::unlink(inum parent_inum, const char *filename)
{
	std::string buf;
	char inum_buf[17]; // 16-digit
	size_t inum_at, name_at;
	int r = OK;
	inum file_inum;

	lc->acquire(parent_inum);

	__lookup(parent_inum, filename, file_inum);
	sprintf(inum_buf, "%016llx", file_inum);
	printf("yfs:unlink:file: inum=%016llx inumbuf=%s\n",
	       file_inum, inum_buf);


	// remove dirent from parent directory
	if (ec->get(parent_inum, buf) != extent_protocol::OK) {
		r = NOENT;
		goto release;
	}

#if DEBUG
	for (unsigned int i=0; i < buf.length(); i++) {
		if (*(buf.c_str() + i) == 0)
			printf("#");
		else
			printf("%c", *(buf.c_str() + i));
	}
	printf("\n");
#endif

	/*
	 * BUGBUG: If a file has name of "00000000f3f3f3f3" and
	 * inum is also 00000000f3f3f3f3, find() will return the index
	 * of inum because inum is stored before filename.
	 * This code will work but it's not elegance.
	 */
	inum_at = buf.find(inum_buf, 0, 16);
	DPRINTF("found inum_at=%d\n", (int)inum_at);
	if (inum_at == std::string::npos) {
		r = IOERR;
		goto release;
	}

	name_at = inum_at + 17;
	buf.erase(name_at, strlen(buf.c_str() + name_at) + 1);
	buf.erase(inum_at, 17);

#if DEBUG
	for (unsigned int i=0; i < buf.length(); i++) {
		if (*(buf.c_str() + i) == 0)
			printf("#");
		else
			printf("%c", *(buf.c_str() + i));
	}
	printf("\n");
#endif

	if (ec->put(parent_inum, buf) != extent_protocol::OK) {
		r = IOERR;
		goto release;
	}

	// remove file entry
	if (ec->remove(file_inum) != extent_protocol::OK) {
		r = IOERR;
		goto release;
	}

release:
	lc->release(parent_inum);
	return r;
}
