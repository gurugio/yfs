//
// Lock demo
//

#include "lock_protocol.h"
#include "lock_client.h"
#include "rpc/rpc.h"
#include <arpa/inet.h>
struct sockaddr_in dst;
lock_client *lc;

int
main(int argc, char *argv[])
{
  int r;

  if(argc != 2){
    fprintf(stderr, "Usage: %s [host:]port\n", argv[0]);
    exit(1);
  }

  make_sockaddr(argv[1], &dst);

  lc = new lock_client(dst);
  r = lc->stat("a");
  printf ("stat returned %d\n", r);

  printf("client->acquire\n");
  r = lc->acquire("a");
  printf("client->acquire ret=%x\n", r);
  printf("client->acquire\n");
  r = lc->acquire("a");
  printf("client->acquire ret=%x\n", r);
  printf("client->acquire\n");
  r = lc->acquire("a");
  printf("client->acquire ret=%x\n", r);

  r = lc->stat("a");
  printf("a->stat=%d\n", r);

  printf("client->release\n");
  r = lc->release("a");
  printf("client->release ret=%x\n", r);

  printf("client->release\n");
  r = lc->release("a");
  printf("client->release ret=%x\n", r);

  printf("client->release\n");
  r = lc->release("a");
  printf("client->release ret=%x\n", r);

  r = lc->stat("a");
  printf("a->stat=%d\n", r);
  
}
