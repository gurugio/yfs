// RPC test and pseudo-documentation.
// takes about a minute to run.

#include "rpc.h"
#include <arpa/inet.h>

// server-side handlers. they must be methods of some class
// to simplify rpcs::reg(). a server process can have handlers
// from multiple classes.
class srv {
public:
  int handle_22(const std::string a, const std::string b, std::string & r);
  int handle_fast(const int a, int &r);
  int handle_slow(const int a, int &r);
  int handle_bigrep(const int a, std::string &r);
  int handle_throw(const int a, int &r);
};

// a handler. a and b are arguments, r is the result.
// there can be multiple arguments but only one result.
// the caller also gets to see the int return value
// as the return value from rpcc::call().
// rpcs::reg() decides how to unmarshall by looking
// at these argument types, so this function definition
// does what a .x file does in SunRPC.
int
srv::handle_22(const std::string a, std::string b, std::string &r)
{
  r = a + b;
  return 0;
}

int
srv::handle_fast(const int a, int &r)
{
  r = a + 1;
  return 0;
}

int
srv::handle_slow(const int a, int &r)
{
  sleep(1);
  r = a + 2;
  return 0;
}

int
srv::handle_bigrep(const int len, std::string &r)
{
  r = std::string((size_t)len, 'x');
  return 0;
}

int
srv::handle_throw(const int len, int &r)
{
  throw 1;
  r = 99;
  return 0;
}

int port;
void * client1(void *xx);
void * client2(void *xx);
void * client3(void *xx);

void
simple_tests(rpcc *c, struct sockaddr_in dst)
{
  // an RPC call to procedure #22.
  // rpcc::call() looks at the argument types to decide how
  // to marshall the RPC call packet, and how to unmarshall
  // the reply packet.
  std::string rep;
  int intret = c->call(dst, 22, "hello", " goodbye", rep);
  assert(intret == 0); // this is what handle_22 returns
  assert(rep == "hello goodbye");

  // small request, big reply (perhaps req via UDP, reply via TCP)
  intret = c->call(dst, 25, 70000, rep, rpcc::to(200000));
  assert(intret == 0);
  assert(rep.size() == 70000);

  // too few arguments
  intret = c->call(dst, 22, "just one", rep);
  assert(intret < 0);

  // too many arguments; proc #23 expects just one.
  intret = c->call(dst, 23, 1001, 1002, rep);
  assert(intret < 0);

  // wrong return value size
  int wrongrep;
  intret = c->call(dst, 23, "hello", " goodbye", wrongrep);
  assert(intret < 0);

  // specify a timeout value to an RPC that should succeed (udp)
  int xx = 0;
  intret = c->call(dst, 23, 77, xx, rpcc::to(3000));
  assert(intret == 0 && xx == 78);

  // specify a timeout value to an RPC that should succeed (tcp)
  {
    std::string arg((size_t) 1000, 'x');
    std::string rep;
    c->call(dst, 22, arg, "x", rep, rpcc::to(3000));
    assert(rep.size() == 1001);
  }

  // huge RPC
  std::string big((size_t) 1000000, 'x');
  intret = c->call(dst, 22, big, "z", rep);
  assert(rep.size() == 1000001);

  // specify a timeout value to an RPC that should timeout (udp)
  struct sockaddr_in dst1 = dst;
  dst1.sin_port = 7661;
  time_t t0 = time(0);
  intret = c->call(dst1, 23, 77, xx, rpcc::to(3000));
  time_t t1 = time(0);
  assert(intret < 0 && (t1 - t0) <= 4);

  // call an rpc handler that throws, and thus should never return
  t0 = time(0);
  xx = 0;
  intret = c->call(dst, 26, 77, xx, rpcc::to(3000));
  t1 = time(0);
  assert(intret < 0 && xx == 0 && (t1 - t0) > 1 && (t1 - t0) <= 4);

  // specify a timeout value to an RPC that should reset (tcp) and fail
  {
    struct sockaddr_in dst1 = dst;
    dst1.sin_port = 7661;
    time_t t0 = time(0);
    std::string arg((size_t) 1000, 'x');
    std::string rep;
    intret = c->call(dst1, 22, arg, "x", rep, rpcc::to(3000));
    time_t t1 = time(0);
    assert(intret < 0 && (t1 - t0) <= 4);
  }

  // specify a timeout value to an RPC that should timeout (tcp) and fail
  {
    struct sockaddr_in dst1 = dst;
    dst1.sin_addr.s_addr = inet_addr("1.2.3.4");
    time_t t0 = time(0);
    std::string arg((size_t) 1000, 'x');
    std::string rep;
    intret = c->call(dst1, 22, arg, "x", rep, rpcc::to(3000));
    time_t t1 = time(0);
    assert(intret < 0 && (t1 - t0) <= 4);
  }

  // can we send to one host while a previous rpc is timing
  // out in tcp connect() to an unreachable destination?
  pthread_t th;
  time(&t0);
  assert(pthread_create(&th, NULL, client3, (void*)c) == 0);
  sleep(1);
  std::string arg((size_t)1000, 'a');
  intret = c->call(dst, 22, arg, "z", rep);
  time(&t1);
  assert(intret == 0);
  assert(rep.size() == 1001);
  assert(t1 - t0 < 3);
  assert(pthread_join(th, NULL) == 0);

}

int
main(int argc, char *argv[])
{
  int ret;

  srandom(getpid());
  port = htons(20000 + (getpid() % 10000));

  // start the server. automatically starts a thread
  // to listen on the UDP port and dispatch calls,
  // each in its own thread. you probably only need
  // one rpcs per process, though it doesn't hurt
  // to have more.
  rpcs server(port);

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  // set stack size to 32K, so we don't run out of memory
  pthread_attr_setstacksize(&attr, 32*1024);

  // register a few server RPC handlers.
  srv service;
  server.reg(22, &service, &srv::handle_22);
  server.reg(23, &service, &srv::handle_fast);
  server.reg(24, &service, &srv::handle_slow);
  server.reg(25, &service, &srv::handle_bigrep);
  server.reg(26, &service, &srv::handle_throw);

  // start the client. creates a port on which to make calls.
  // starts a thread to listen for replies and hand them to
  // the correct waiting caller thread. there should probably
  // be only one rpcc per process. you probably need only one
  // rpcc per process, though it doesn't hurt to have more.
  rpcc client;

  // server's address.
  struct sockaddr_in dst;
  bzero(&dst, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_addr.s_addr = inet_addr("127.0.0.1");
  dst.sin_port = port;

  simple_tests(&client, dst);

  // create threads that make lots of calls in parallel,
  // to test thread synchronization for concurrent calls
  // and dispatches.
  {
    int nt = 100;
    pthread_t th[nt];
    for(int i = 0; i < nt; i++){
      ret = pthread_create(&th[i], &attr, client1, (void *) &client);
      assert(ret == 0);
    }
    for(int i = 0; i < nt; i++){
      assert(pthread_join(th[i], NULL) == 0);
    }
  }

  // test loss, timeout, retransmission.
  server.setlossy(true);
  {
    int nt = 10;
    pthread_t th[nt];
    for(int i = 0; i < nt; i++){
      ret = pthread_create(&th[i], &attr, client2, (void *) &client);
      assert(ret == 0);
    }
    for(int i = 0; i < nt; i++){
      assert(pthread_join(th[i], NULL) == 0);
    }
  }

  printf("rpctest OK\n");

  exit(0);
}

void *
client1(void *xx)
{
  rpcc *c = (rpcc *) xx;
  struct sockaddr_in dst;

  bzero(&dst, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_addr.s_addr = inet_addr("127.0.0.1");
  dst.sin_port = port;

  // test concurrency.
  for(int i = 0; i < 100; i++){
    int arg = (random() % 2000);
    std::string rep;
    c->call(dst, 25, arg, rep);
    assert(rep.size() == (size_t) arg);
  }

  // test rpc replies coming back not in the order of
  // the original calls -- i.e. does xid reply dispatch work.
  for(int i = 0; i < 10; i++){
    int which = (random() % 2);
    int arg = (random() % 1000);
    int rep;
    c->call(dst, which ? 23 : 24, arg, rep);
    assert(rep == (which ? arg+1 : arg+2));
  }

  return 0;
}

void *
client2(void *xx)
{
  rpcc *c = (rpcc *) xx;
  struct sockaddr_in dst;

  bzero(&dst, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_addr.s_addr = inet_addr("127.0.0.1");
  dst.sin_port = port;

  time_t t1;
  time(&t1);

  while(time(0) - t1 < 10){
    int arg = (random() % 2000);
    std::string rep;
    c->call(dst, 25, arg, rep);
    assert(rep.size() == (size_t) arg);
  }

  return 0;
}

void *
client3(void *xx)
{
  rpcc *c = (rpcc *) xx;
  struct sockaddr_in dst;

  bzero(&dst, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_addr.s_addr = inet_addr("1.2.3.4");
  dst.sin_port = port;

  time_t t1;
  time(&t1);

  std::string arg((size_t)1000, 'x');
  std::string rep;
  c->call(dst, 22, arg, "xx", rep, rpcc::to(30000));

  assert(time(0) - t1 > 20);

  return 0;
}
