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

// - If a file named @name already exists in @parent, return EXIST.
// - Pick an ino (with type of yfs_client::inum) for file @name. 
//   Make sure ino indicates a file, not a directory!
// - Create an empty extent for ino.
// - Add a <name, ino> entry into @parent.
// - Change the parent's mtime and ctime to the current time/date
//   (this may fall naturally out of your extent server code).
// - On success, store the inum of newly created file into @e->ino, 
//   and the new file's attribute into @e->attr. Get the file's
//   attributes with getattr().
int yfs_client::createfile(inum parent_inum, inum file_inum,
			   const char *name, mode_t mode)
{
	int r = OK;
	std::list<dirent *> files_in_parent;
	std::list<dirent *>::iterator it_dirent;
	dirent *new_file;

	printf("create %016llx %s\n", file_inum, name);

	if (directories.find(parent_inum) == directories.end()) {
		// parent does not exist
		r = NOENT;
		goto exit_error;
	}

	files_in_parent = directories[parent_inum];

	// search name in files_in_parent list
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

	for (it_dirent = files_in_parent.begin();
	     it_dirent != files_in_parent.end(); it_dirent++) {
		printf("  new-file: %s\n", (*it_dirent)->name.c_str());
		if ((*it_dirent)->inum == file_inum) {
			r = EXIST;
			goto exit_error;
		}
	}

	if (ec && ec->put(file_inum, std::string(name)) != extent_protocol::OK) {
		r = IOERR;
		goto exit_error;
	}

exit_error:
	return r;
}

