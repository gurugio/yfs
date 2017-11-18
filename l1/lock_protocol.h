// lock protocol

#ifndef protocol_h
#define protocol_h

#include "rpc.h"

class lock_protocol {
 public:
  enum xxstatus { OK, RETRY, RPCERR, NOENT, IOERR };
  typedef int status;
  enum rpc_numbers {
    acquire = 0x7001,
    release,
    subscribe,
    stat
  };
};

#endif 
