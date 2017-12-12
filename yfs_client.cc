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

  printf("getfile %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  fin.atime = a.atime;
  fin.mtime = a.mtime;
  fin.ctime = a.ctime;
  fin.size = a.size;
  printf("getfile %016llx -> sz %llu\n", inum, fin.size);

 release:

  return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  int r = OK;

  printf("getdir %016llx\n", inum);
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
		printf("  lookup: found-file: %s\n",
		       (*it_dirent)->name.c_str());
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
	dirent *new_file;
	extent_protocol::attr a;

	printf("create %016llx %s\n", file_inum, name);

	if (directories.find(parent_inum) == directories.end()) {
		r = NOENT;
		goto exit_error;
	}

	files_in_parent = directories[parent_inum];

	for (it_dirent = files_in_parent.begin();
	     it_dirent != files_in_parent.end(); it_dirent++) {
		printf("  current-file: %s\n", (*it_dirent)->name.c_str());
		if ((*it_dirent)->inum == file_inum) {
			r = EXIST;
			goto exit_error;
		}
	}

	new_file = new dirent;
	new_file->name = std::string(name);
	new_file->inum = file_inum;
	files_in_parent.push_back(new_file);
	directories[parent_inum] = files_in_parent;

	// change mtime, ctime of parent directory
	if (ec && ec->getattr(parent_inum, a) != extent_protocol::OK) {
		r = IOERR;
		goto exit_error;
	}

	a.mtime = a.ctime = time(NULL);
	if (ec && ec->putattr(file_inum, a) != extent_protocol::OK) {
		r = IOERR;
		goto exit_error;
	}


	if (ec && ec->put(file_inum, std::string(name)) != extent_protocol::OK) {
		r = IOERR;
		goto exit_error;
	}

exit_error:
	return r;
}
