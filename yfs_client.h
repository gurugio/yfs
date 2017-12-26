#ifndef yfs_client_h
#define yfs_client_h

#include <string>
//#include "yfs_protocol.h"
#include "extent_client.h"
#include <vector>

#define DEBUG 0
#define DPRINTF(...) do {						\
		if (DEBUG) printf(__VA_ARGS__);			\
	} while (0);

class yfs_client {
  extent_client *ec;
 public:

  typedef unsigned long long inum;
  enum xxstatus { OK, RPCERR, NOENT, IOERR, EXIST };
  typedef int status;

  struct fileinfo {
    unsigned long long size;
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirinfo {
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirent {
    std::string name;
    yfs_client::inum inum;
  };

 private:
  static std::string filename(inum);
  static inum n2i(std::string);
 public:

  yfs_client(std::string, std::string);

  bool isfile(inum);
  bool isdir(inum);

  int getfile(inum, fileinfo &);
  int getdir(inum, dirinfo &);

  inum get_nextid(int);
  int createfile(inum, const char *, inum *);
  int createdir(inum, const char *, inum *);
  int lookup(inum, const char *, inum &);
  int add_dirent(inum, const char *, inum);
  int readdir(inum, struct dirent **, int *);
  int resizefile(inum, size_t);
  int writefile(inum, const char *, size_t, off_t);
  int readfile(inum, size_t, off_t, std::string &);
};

#endif 
