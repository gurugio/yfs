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

yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
	if (extent_dst != "0")
		ec = new extent_client(extent_dst);
	else
		ec = NULL;
	directories[1] = root;
	if (ec && ec->put(1, std::string("/")) != extent_protocol::OK) {
		printf("ERROR: failed to create root directory");
	}
	// TODO: use lock_dst to create lock_server
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

  printf("yfs:getfile %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
	r = IOERR;
	goto release;
  }

  fin.atime = a.atime;
  fin.mtime = a.mtime;
  fin.ctime = a.ctime;
  fin.size = a.size;
  printf("yfs:getfile %016llx -> sz %llu\n", inum, fin.size);

 release:

  return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  int r = OK;

  printf("yfs:getdir %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
	r = IOERR;
	goto release;
  }
  din.atime = a.atime;
  din.mtime = a.mtime;
  din.ctime = a.ctime;

 release:
  return r;
}

int yfs_client::lookup(inum parent_inum, const char *name, inum &file_inum)
{
	std::list<dirent *> files_in_parent;
	std::list<dirent *>::iterator it_dirent;

	printf("lookup: parent_inum=%llu name=%s\n", parent_inum, name);

	if (directories.find(parent_inum) == directories.end()) {
		return NOENT;
	}

	printf("  lookup: found parent\n");
	files_in_parent = directories[parent_inum];

	for (it_dirent = files_in_parent.begin();
		 it_dirent != files_in_parent.end(); it_dirent++) {
		printf("  lookup: file-list: %s(%01llx)\n",
			   (*it_dirent)->name.c_str(),
			   (*it_dirent)->inum);
		if ((*it_dirent)->name == name) {
			file_inum = (*it_dirent)->inum;
			return EXIST;
		}
	}
	return NOENT;
}

std::list<yfs_client::dirent *> yfs_client::readdir(inum dir_inum)
{
	printf("yfs: readdir: dir_inum=%llx\n", dir_inum);
	return directories[dir_inum];
}

int yfs_client::createfile(inum parent_inum, inum file_inum,
			   const char *name, mode_t mode)
{
	int r = OK;
	std::list<dirent *> files_in_parent;
	std::list<dirent *>::iterator it_dirent;
	dirent *new_dirent;
	fileent *new_fileent;
	extent_protocol::attr a;

	printf("yfs: create: %016llx %s\n", file_inum, name);

	if (directories.find(parent_inum) == directories.end()) {
		r = NOENT;
		goto exit_error;
	}

	files_in_parent = directories[parent_inum];

	for (it_dirent = files_in_parent.begin();
		 it_dirent != files_in_parent.end(); it_dirent++) {
		//printf("  current-file: %s\n", (*it_dirent)->name.c_str());
		if ((*it_dirent)->inum == file_inum) {
			r = EXIST;
			goto exit_error;
		}
	}

	// store dir info
	new_dirent = new dirent;
	new_dirent->name = std::string(name);
	new_dirent->inum = file_inum;
	files_in_parent.push_back(new_dirent);
	directories[parent_inum] = files_in_parent;
	printf("yfs: create: insert new file with inum=%llx\n", file_inum);

	// change mtime, ctime of parent directory
	printf("yfs: create: change attr of parent dir %llx\n", parent_inum);
	if (ec && ec->getattr(parent_inum, a) != extent_protocol::OK) {
		r = IOERR;
		goto exit_error;
	}
	a.mtime = a.ctime = time(NULL);
	if (ec && ec->putattr(parent_inum, a) != extent_protocol::OK) {
		r = IOERR;
		goto exit_error;
	}

	// create file buffer
	new_fileent = new fileent;
	new_fileent->name = std::string(name);
	new_fileent->size = 0;
	new_fileent->buf = NULL;
	files.insert(std::make_pair(file_inum, new_fileent));

	printf("yfs: create: put %llx\n", file_inum);
	if (ec && ec->put(file_inum, std::string(name)) != extent_protocol::OK) {
		r = IOERR;
		goto exit_error;
	}

exit_error:
	if (r != OK) printf("yfs: create: exit with error=%d\n", r);
	return r;
}

int yfs_client::resizefilebuf(inum file_inum, unsigned int size)
{
	std::map<inum, fileent *>::iterator it_fileent;
	struct fileent *entry;

	printf("yfs:setfilebuf: inum=%llu size=%u\n", file_inum, size);

	it_fileent = files.find(file_inum);
	if (it_fileent == files.end()) {
		return NOENT;
	}

	printf("yfs:setfilebuf: found file\n");
	entry = it_fileent->second;

	if (size > entry->size) {
		char *newbuf = (char *)calloc(size, sizeof(char));
		memcpy(newbuf, entry->buf, entry->size);
		free(entry->buf);
		entry->buf = newbuf;
	} else if (size < entry->size) {
		char *newbuf = (char *)calloc(size, sizeof(char));
		memcpy(newbuf, entry->buf, size);
		free(entry->buf);
		entry->buf == newbuf;
	} else {
		// do nothing
	}

	return OK;
}
