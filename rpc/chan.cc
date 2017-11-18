// To Do:
// find out why freebsd runs out of threads, even in wanrpc server
//   maybe -lc_r instead of -lpthread?
// pool of worker threads? (really for rpc.cc)
// why does it burn up so much CPU? in wanrpc.
// ~cchan(), ~schan() (tcpchans, mutexes, active threads)
// GC might make threads+locks+delete much easier.
//   easy in xv6 because no free()
// writev (for length + data)
// tcp back-pressure to avoid huge inq
// OSX pthread_cancel() only takes effect on explict call
//   to pthread_testcancel(). maybe that's safest anyway
//   since it gives the thread some chance to release locks.
//   though it's hard to see how to safely terminate a thread
//   that holds nested locks.
// fifo.h cancel stuff is ugly. create a cxxthread.h and cxxthread.cc
//   with c++-friendly threads/locks/&c?
// coherent plan for killing threads in lock-friendly way?
//   new cond_wait that returns periodically regardless
//   check in mutex_unlock "does someone want this thread to die?"
//   check that it only holds the one lock
//   OR a kill-thread-only-if-it-is-waiting-where-I-expect
//     so you don't kill a thread that happens to be in a function
//     whose invariants you don't understand

// tcpchan deletion strategy:
// 1. if created by cchan::send_tcp, then deleted by its
//    cchan::tcp_loop. nobody in tcpchan::send() because
//    protected by cchan::tcpchans_m. nobodyin tcpchan::recv()
//    because only called by cchan::tcp_loop. tcpchan does
//    not set isdead until the output thread exits.
// 2. if created by schan::tcp_loop(), then deleted by
//    schan::tcp_loop1. no-one in tcpchan::send because
//    schan::send_tcp calls it inside tcpchans_m lock.
//    no-one else in tcpchan::recv b/c schan::tcp_loop1 is
//    the only caller.
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/types.h>
#include <arpa/inet.h>
#include <signal.h>
#include <strings.h>
#include <netinet/tcp.h>
#include "method_thread.h"
#include "chan.h"

cchan::cchan()
{
  assert(pthread_mutex_init(&tcpchans_m, 0) == 0);

  udp = socket(AF_INET, SOCK_DGRAM, 0);
  if(udp < 0){
    perror("socket");
    exit(1);
  }

  // force allocation of a UDP port
  sockaddr_in sin;
  bzero(&sin, sizeof(sin));
  sin.sin_family = AF_INET;
  assert(bind(udp, (sockaddr*)&sin, sizeof(sin)) == 0);

  // what's our UDP port?
  socklen_t slen = sizeof(sin);
  assert(getsockname(udp, (sockaddr*)&sin, &slen) == 0);
  my_port = sin.sin_port;

  method_thread(this, true, &cchan::udp_loop);
}

void
cchan::send_udp(sockaddr_in sin, std::string pdu)
{
  int ret = sendto(udp, pdu.data(), pdu.size(), 0,
                   (sockaddr*)&sin, sizeof(sin));
  if(ret < 0)
    perror("sendto");
}

bool
operator<(sockaddr_in a, sockaddr_in b)
{
  if(a.sin_family < b.sin_family)
    return true;
  if(a.sin_family > b.sin_family)
    return false;

  if(ntohl(a.sin_addr.s_addr) < ntohl(b.sin_addr.s_addr))
    return true;
  if(ntohl(a.sin_addr.s_addr) > ntohl(b.sin_addr.s_addr))
    return false;

  if(ntohs(a.sin_port) < ntohs(b.sin_port))
    return true;
  if(ntohs(a.sin_port) > ntohs(b.sin_port))
    return false;

  return false;
}

// set up a connection to a server if we don't already have one
void
cchan::setup_tcp(sockaddr_in sin)
{
  assert(pthread_mutex_lock(&tcpchans_m) == 0);
  if(tcpchans.count(sin) < 1){
    tcpchan *ch = new tcpchan(sin, my_port);
    tcpchans[sin] = ch;
    method_thread(this, true, &cchan::tcp_loop, sin, ch);
  }
  assert(pthread_mutex_unlock(&tcpchans_m) == 0);
}

void
cchan::send_tcp(sockaddr_in sin, std::string pdu)
{
  setup_tcp(sin);

  // lock to prevent race w/ deletion as well
  // as to avoid wrecking tcpchans[].
  assert(pthread_mutex_lock(&tcpchans_m) == 0);
  if(tcpchans.count(sin) > 0){
    tcpchan *ch = tcpchans[sin];
    ch->send(pdu);
  } else {
    printf("cchan::send_tcp oops\n");
  }
  assert(pthread_mutex_unlock(&tcpchans_m) == 0);
}


void
cchan::send(sockaddr_in sin, std::string pdu,
            int &timeout_recommendation)
{
  // use UDP if send small msgs at < 5 per second
  // (protected by rpcc mutex)
  if(pdu.size() < 100){
    time_t now;
    time(&now);
    if(now != rate_t || rate_n < 5){
      send_udp(sin, pdu);
      timeout_recommendation = 5;
      setup_tcp(sin); // in case reply is big
      if(now == rate_t){
        rate_n++;
      } else {
        rate_n = 0;
        rate_t = now;
      }
      return;
    }
  }
  send_tcp(sin, pdu);
  timeout_recommendation = 64;
}

// copy PDUs from a tcpchan to inq
void
cchan::tcp_loop(sockaddr_in key, tcpchan *ch)
{
  while(!ch->dead()){
    std::string pdu = ch->recv();
    inq.enq(pdu);
  }

  assert(pthread_mutex_lock(&tcpchans_m) == 0);
  assert(tcpchans[key] == ch);
  tcpchans.erase(key);
  assert(pthread_mutex_unlock(&tcpchans_m) == 0);

  delete ch;
}

std::string
cchan::recv()
{
  return inq.deq();
}

void
cchan::udp_loop()
{
  int max = 70000;
  char *p = new char[max];
  while(1){
    int cc = ::recv(udp, p, max, 0);
    if(cc < 0){
      perror("rpcc recvfrom");
      break;
    } else {
      inq.enq(std::string(p, cc));
    }
  }
  delete p;
}

schan::schan(int port)
  : next_key(1), lossy(false), lossy_percent(10)
{
  assert(pthread_mutex_init(&tcpchans_m, 0) == 0);
  assert(pthread_cond_init(&tcpchans_c, 0) == 0);

  udp = socket(AF_INET, SOCK_DGRAM, 0);
  if(udp < 0){
    perror("socket");
    exit(1);
  }

  /**
   * Setting lossy on the server does horrible things to the
   * connection, rather than simply dropping packets.
  char *loss_env = getenv("RPC_LOSSY");
  if(loss_env != NULL){
    lossy_percent = atoi(loss_env);
    if(lossy_percent > 0){
      setlossy(true);
    }
  }
  */

  struct sockaddr_in sin;
  bzero(&sin, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_port = port;
  if(bind(udp, (struct sockaddr *)&sin, sizeof(sin)) < 0){
    perror("schan udp bind");
    exit(1);
  }

  method_thread(this, true, &schan::udp_loop);

  tcp = socket(AF_INET, SOCK_STREAM, 0);
  if(tcp < 0){
    perror("socket");
    exit(1);
  }
  int yes = 1;
  setsockopt(tcp, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  setsockopt(tcp, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
  if(bind(tcp, (sockaddr *)&sin, sizeof(sin)) < 0){
    perror("schan tcp bind");
    exit(1);
  }
  if(listen(tcp, 1000) < 0){
    perror("schan tcp listen");
    exit(1);
  }

  method_thread(this, true, &schan::tcp_loop);
}

void
schan::setlossy(bool x)
{
  lossy = x;
}

// wait for each new tcp connection, turn it into a tcpchan,
// start a thread to read from that tcpchan.
void
schan::tcp_loop()
{
  while(1){
    sockaddr_in sin;
    socklen_t slen = sizeof(sin);
    int s1 = accept(tcp, (sockaddr*)&sin, &slen);
    if(s1 < 0){
      perror("schan accept");
      break;
    }
    method_thread(this, true, &schan::tcp_loop1, s1);
  }
}

// just accept()ed a connection to an schan.
// wait for input on a single tcpchan, add to inq.
// blocks inside tcpchan::recv().
void
schan::tcp_loop1(int s)
{
  int yes = 1;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

  tcpchan *ch = new tcpchan(s);
  int key = next_key++;

  assert(pthread_mutex_lock(&tcpchans_m) == 0);
  assert(tcpchans.count(key) == 0);
  tcpchans[key] = ch;
  pthread_cond_broadcast( &tcpchans_c );
  assert(pthread_mutex_unlock(&tcpchans_m) == 0);

  while(!ch->dead()){
    if(lossy && (random() % 100) < lossy_percent)
      ch->lose();
    std::string pdu = ch->recv();
    inq.enq(inbuf(pdu, chan_key(key)));
  }

  assert(pthread_mutex_lock(&tcpchans_m) == 0);
  assert(tcpchans[key] == ch);
  tcpchans.erase(key);
  assert(tcpchans.count(key) == 0);
  assert(pthread_mutex_unlock(&tcpchans_m) == 0);

  delete ch;
}

void
schan::udp_loop()
{
  int max = 70000;
  char *p = new char[max];

  while(1){
    sockaddr_in sin;
    socklen_t slen = sizeof(sin);
    bzero(&sin, sizeof(sin));
    int cc = recvfrom(udp, p, max, 0,
                      (struct sockaddr *) &sin,
                      &slen);
    if(cc < 0){
      perror("rpcc recvfrom");
      break; // XXX can't happen...
    } else {
      std::string pdu(p, cc);
      inq.enq(inbuf(pdu, chan_key(sin)));
    }
  }

  delete p;
}

// key should be passed again to send(), to ensure that
// RPC reply goes back to where the request came from.
void
schan::recv(std::string &pdu, chan_key &key)
{
  inbuf b = inq.deq();
  pdu = b.s;
  key = b.key;
}

int
schan::find_him(sockaddr_in key)
{
  assert(pthread_mutex_lock(&tcpchans_m) == 0);

  // it is worth waiting a small amount of time for the client
  // to set up a TCP channel, otherwise the big-PDU packet about to
  // be sent will just have to be retransmitted
  static const int to_wait_ms = 300;
  struct timespec deadline;
  bool deadline_made = false;
  bool deadline_reached = false;
  int nkey = 0;

  do {

    std::map<int, tcpchan*>::iterator i;
    for(i = tcpchans.begin(); i != tcpchans.end(); i++){
      tcpchan *ch = i->second;
      if(ch->him().sin_addr.s_addr == key.sin_addr.s_addr &&
	 ch->him().sin_port == key.sin_port &&
	 ch->dead() == false){
	nkey = i->first;
      }
    }

    if( nkey == 0 ) {
      if( !deadline_made ) {
	deadline_made = true;
	struct timeval now;
	gettimeofday(&now, NULL);
	// convert to millisec, add timeout, convert back
	long double msec = (now.tv_sec*1000.0 + now.tv_usec/1000.0) + 
	  to_wait_ms*1.0;
	deadline.tv_sec = (long) (msec/1000);
	deadline.tv_nsec = 
	  ((long) ((msec-((long double) deadline.tv_sec*1000.0))*1000))*1000;
      }
      int ret = pthread_cond_timedwait( &tcpchans_c, &tcpchans_m, &deadline );
      if( ret == ETIMEDOUT ) {
	deadline_reached = true;
      }
    }

  } while( nkey == 0 && !deadline_reached );

  assert(pthread_mutex_unlock(&tcpchans_m) == 0);

  return nkey;
}

void
schan::send(std::string pdu, chan_key key)
{
  int nkey;

  if(key.key){
    send_tcp(key.key, pdu);
  } else if(key.sin.sin_family &&
            pdu.size() > 1000 &&
            (nkey = find_him(key.sin))){
    // send a long reply back via TCP even if req came by UDP,
    // assuming we can find a TCP connection from same client.
    send_tcp(nkey, pdu);
  } else if(key.sin.sin_family){
    // udp
    int ret = sendto(udp, pdu.data(), pdu.size(), 0,
                     (sockaddr*)&key.sin, sizeof(key.sin));
    if(ret < 0)
      perror("sendto");
  } else {
    printf("schan::send bad key\n");
  }
}

void
schan::send_tcp(int key, std::string pdu)
{
  tcpchan *ch = 0;
  assert(pthread_mutex_lock(&tcpchans_m) == 0);

  if(tcpchans.count(key) > 0 && (ch = tcpchans[key])->dead() == false){
    ch->send(pdu);
  } else {
    printf("schan::send_tcp cannot find key %d ch %p count %lu dead %u\n",
           key, ch, tcpchans.count(key),
           (tcpchans.count(key) > 0)?ch->dead():-1);
  }

  assert(pthread_mutex_unlock(&tcpchans_m) == 0);
}

tcpchan::tcpchan(sockaddr_in dst, short my_port)
  : s(-1), th(0), isdead(false), isconnecting(true)
{
  signal(SIGPIPE, SIG_IGN);
  assert(pthread_mutex_init(&connecting_m, 0) == 0);
  assert(pthread_cond_init(&connecting_c, 0) == 0);
  bzero(&_him, sizeof(_him));
  th = method_thread(this, false, &tcpchan::do_connect, dst, my_port);
}

tcpchan::tcpchan(int xs)
  : s(xs), th(0), isdead(false), isconnecting(false)
{
  signal(SIGPIPE, SIG_IGN);
  assert(pthread_mutex_init(&connecting_m, 0) == 0);
  assert(pthread_cond_init(&connecting_c, 0) == 0);

  // read client's UDP sockaddr_in
  socklen_t hlen = sizeof(_him);
  if(getpeername(s, (sockaddr*)&_him, &hlen) != 0)
    isdead = true;
  // Don't die if we can't read the port, because the client
  // might have already successfully finished it's communication
  // and closed the port
  if(xread(&_him.sin_port, 2, false) != 0)
    isdead = true;

  th = method_thread(this, false, &tcpchan::output_loop);
}

tcpchan::~tcpchan()
{
  assert(isdead || th == 0);
  close(s);
  s = -1;
  // must wait for output thread to die, since it is still
  // using this object. die() pthread_cancel()ed it.
  // the thread doesn't die immediately, instead the cond_wait()
  // in fifo.h wakes up every 10 seconds and checks if it has
  // been canceled. and if the thread is in connect()
  // it can take longer. so deleting a tcpchan may take
  // a minute or two!
  if(th)
    assert(pthread_join(th, 0) == 0);
  th = 0;
  assert(pthread_mutex_destroy(&connecting_m) == 0);
  assert(pthread_cond_destroy(&connecting_c) == 0);
}

// output thread
void
tcpchan::do_connect(sockaddr_in dst, short my_port)
{
  bool ok = true;

  // tcpchan::tcpchan() also sets th but we might execute
  // first, and th must be set for die().
  th = pthread_self();

  s = socket(AF_INET, SOCK_STREAM, 0);
  if(s < 0){
    perror("tcpchan socket");
    ok = false;
  }
  if(connect(s, (sockaddr*)&dst, sizeof(dst)) < 0){
	 printf("tcpchan connect problem to %s:%d\n", inet_ntoa(dst.sin_addr), ntohs(dst.sin_port));
    perror("tcpchan connect");
    ok = false;
  }
  int yes = 1;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

  // tell other side cchan's official port, in case it needs
  // to send replies to UDP RPC requests via TCP.
  write(s, &my_port, 2);
 
  assert(pthread_mutex_lock(&connecting_m) == 0);
  isconnecting = false;
  assert(pthread_cond_broadcast(&connecting_c) == 0);
  assert(pthread_mutex_unlock(&connecting_m) == 0);
 
  if(ok == false)
    die();
  output_loop();
}

// output thread
void
tcpchan::output_loop()
{
  // tcpchan::tcpchan() also sets th but we might execute
  // first, and th must be set for die().
  th = pthread_self();

  while(1){
    std::string pdu = outq.deq();
    send1(pdu);
  }
}

// output thread
void
tcpchan::send1(std::string pdu)
{
  unsigned long len = htonl(pdu.size());
  if(write(s, &len, sizeof(len)) != sizeof(len)){
    perror("tcpchan write");
    die();
  }
  if(write(s, pdu.data(), pdu.size()) != (ssize_t) pdu.size()){
    perror("tcpchan write");
    die();
  }
}

// for debugging -- do something bad to the connection.
void
tcpchan::lose()
{
  int x = (random() % 3);
  if(x == 0){
    shutdown(s, SHUT_RD);
  } else if(x == 1){
    shutdown(s, SHUT_WR);
  } else {
    close(s);
    s = -1;
  }
}

// non-blocking even if output thread is e.g. in connect()
// called inside cchan or schan mutex
void
tcpchan::send(std::string pdu)
{
  outq.enq(pdu);
}

// read exactly n bytes.
int
tcpchan::xread(void *xp, unsigned int n, bool die_on_error)
{
  char *p = (char *) xp;
  unsigned int i = 0;
  while(i < n){
    int cc = read(s, p + i, n - i);
    if(cc < 0){
      if(die_on_error){
	perror("tcpchan read");
	die();
      }
      return -1;
    }
    if(cc == 0){
      die();
      return -1;
    }
    i += cc;
  }
  return 0;
}

// blocks and reads from the tcp socket.
// called inside cchan or schan mutex
std::string
tcpchan::recv()
{
  unsigned long len;

  while(1){
    bool done = false;
    assert(pthread_mutex_lock(&connecting_m) == 0);
    if(isconnecting)
      assert(pthread_cond_wait(&connecting_c, &connecting_m) == 0);
    else
      done = true;
    assert(pthread_mutex_unlock(&connecting_m) == 0);
    if(done)
      break;
  }

  if(xread(&len, sizeof(len)) != 0)
    return "";
  len = ntohl(len);
  if(len > 10*1024*1024){
    printf("tcpchan:recv len %lu too big\n", len);
    die();
    return "";
  }
  char *p = (char *) malloc(len);
  if(xread(p, len) != 0){
    free(p);
    return "";
  }
  std::string pdu(p, len);
  free(p);
  return pdu;
}

// kill the output thread and set isdead, so that the owning
// schan/cchan will delete us.
// might be called from within the output thread...
void
tcpchan::die()
{
  isdead = true;
  if(th)
    pthread_cancel(th);
}

#if 0
int
main()
{
  int port = htons(4772);

  cchan cc;
  schan ss(port);

  struct sockaddr_in sin;
  bzero(&sin, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = inet_addr("127.0.0.1");
  sin.sin_port = port;

  cc.send(sin, "hello");

  std::string x;
  schan::chan_key key;
  ss.recv(x, key);
  ss.send(x + " and goodbye", key);

  std::string y = cc.recv();
  printf("cc.recv said %s\n", y.c_str());

  return 0;
}
#endif
