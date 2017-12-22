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
yfs_client::getfile(inum file_inum, fileinfo &fin)
{
	std::map<inum, fileent *>::iterator it_fileent;
	struct fileent *entry;
	extent_protocol::attr a;
	int r = OK;

	printf("yfs:getfile %016llx\n", file_inum);

	if (ec->getattr(file_inum, a) != extent_protocol::OK) {
		r = IOERR;
		goto release;
	}

	// BUGBUG:file size is stored in fileent, not extent server
	it_fileent = files.find(file_inum);
	if (it_fileent == files.end()) {
		return NOENT;
	}

	entry = it_fileent->second;

	fin.atime = a.atime;
	fin.mtime = a.mtime;
	fin.ctime = a.ctime;
	fin.size = entry->size;
	printf("yfs:getfile %016llx -> sz %llu\n", file_inum, fin.size);

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
	// a.mtime = a.ctime = time(NULL);
	// if (ec && ec->putattr(parent_inum, a) != extent_protocol::OK) {
	//	r = IOERR;
	//	goto exit_error;
	// }

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

void yfs_client::resizefilebuf_helper(struct fileent *entry,
				      unsigned int newsize)
{
	char *newbuf;

	// BUGBUG: don't care memory allocation failure
	if (newsize > entry->size) {
		newbuf = (char *)calloc(newsize, sizeof(char));
		memcpy(newbuf, entry->buf, entry->size);
		free(entry->buf);
		entry->buf = newbuf;
		entry->size = newsize;
	} else if (newsize < entry->size) {
		newbuf = (char *)calloc(newsize, sizeof(char));
		memcpy(newbuf, entry->buf, newsize);
		free(entry->buf);
		entry->buf = newbuf;
		entry->size = newsize;
	} else {
		// do nothing
	}
}

int yfs_client::setfileattr(inum file_inum, struct stat &st)
{
	std::map<inum, fileent *>::iterator it_fileent;
	struct fileent *entry;

	printf("yfs:setfileattr: inum=%llx mtime=%lu size=%u\n",
		   file_inum, st.st_mtime, (unsigned int)st.st_size);

	it_fileent = files.find(file_inum);
	if (it_fileent == files.end()) {
		return NOENT;
	}

	entry = it_fileent->second;
	resizefilebuf_helper(entry, st.st_size);
#if 0
	a.mtime = st.st_mtime;
	a.ctime = st.st_ctime;
	a.atime = st.st_atime;
	a.size  = st.st_size;
	printf("yfs:setfileattr: time:%lu %lu %lu size:%llu\n",
		   a.atime, a.mtime, a.ctime, a.size);
	if (ec && ec->putattr(file_inum, a) != extent_protocol::OK) {
		printf("yfs:resizefilebuf: extent error\n");
		return RPCERR;
	}
#endif
	return OK;
}

int yfs_client::writefilebuf(inum file_inum, const char *buf,
							 size_t size, off_t off)
{
	std::map<inum, fileent *>::iterator it_fileent;
	struct fileent *entry;

	printf("yfs:writefilebuf: inum=%llu size=%u off=%u\n",
	       file_inum, (int)size, (int)off);

	it_fileent = files.find(file_inum);
	if (it_fileent == files.end()) {
		return -1;
	}

	entry = it_fileent->second;

	if ((size + off) > entry->size) {
		char *newbuf;
		newbuf = (char *)calloc((size + off), sizeof(char));
		memcpy(newbuf, entry->buf, entry->size);
		free(entry->buf);
		entry->buf = newbuf;
		entry->size = (size + off);
	}
	printf("yfs:writefilebuf: newsize=%u\n", (int)entry->size);

	memcpy(&entry->buf[off], buf, size);
	return size;
}

int yfs_client::readfilebuf(inum file_inum, std::string &buf,
							size_t size, off_t off)
{
	std::map<inum, fileent *>::iterator it_fileent;
	struct fileent *entry;

	printf("yfs:readfilebuf: inum=%llu size=%u\n", file_inum, (int)size);

	it_fileent = files.find(file_inum);
	if (it_fileent == files.end()) {
		return -1;
	}

	printf("yfs:readfilebuf: found file\n");
	entry = it_fileent->second;

	if (off > entry->size) {
		return 0;
	} else if (size + off > entry->size) {
		size = entry->size - off;
	}

	buf = std::string(&entry->buf[off], size);
	//buf = std::string(tmp, 5);
	return size;
}
