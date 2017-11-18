// server worker thread pool
// TCP transport (probably needed for good wide-area behavior)
// libarpc lets you send and serve on the same port, do we need that?
#include <unistd.h>

#include "rpc.h"
#include "method_thread.h"
#ifdef VIVALDI
#include "vivaldi.h"
#endif
#include <arpa/inet.h>
#include <netdb.h>
#include <strings.h>
#include <sys/select.h>
#include <time.h>

rpcc::rpcc()
{
  xid = 1;
  _vivaldi = NULL;

  assert(pthread_mutex_init(&m, 0) == 0);
  assert(pthread_mutex_init(&_timeout_lock, 0) == 0);
  assert(pthread_cond_init(&_timeout_cond, 0) == 0);
  _next_timeout.tv_sec = 0;

  if(method_thread(this, true, &rpcc::clock_loop) == 0){
    perror("rpcc::rpcc pthread_create");
    exit(1);
  }
  if(method_thread(this, true, &rpcc::chan_loop) == 0){
    perror("rpcc::rpcc pthread_create");
    exit(1);
  }
}

rpcc::~rpcc()
{
  assert(pthread_mutex_destroy(&m) == 0);
  assert(pthread_mutex_destroy(&_timeout_lock) == 0);
  assert(pthread_cond_destroy(&_timeout_cond) == 0);
}

rpcc::caller::caller(int xxid, unmarshall *xun, uint32_t ip, uint16_t port, double when)
  : xid(xxid), un(xun), done(false), senttime(when), other_ip(ip), other_port(port)
{
  assert(pthread_cond_init(&c, 0) == 0);
  assert(pthread_mutex_init(&m, 0) == 0);
}

rpcc::caller::~caller()
{
  assert(pthread_mutex_destroy(&m) == 0);
  assert(pthread_cond_destroy(&c) == 0);
}

const rpcc::TO rpcc::to_inf = { -1 };

/* Subtract the `struct timeval' values X and Y,
   storing the result in RESULT.
   Return 1 if the difference is negative, otherwise 0.  */
// taken from http://www.delorie.com/gnu/docs/glibc/libc_428.html
int
timeval_subtract (struct timeval *result, struct timeval *x, struct timeval *y)
{
  struct timeval y2 = *y;

  /* Perform the carry for the later subtraction by updating y. */
  if (x->tv_usec < y2.tv_usec) {
    int nsec = (y2.tv_usec - x->tv_usec) / 1000000 + 1;
    y2.tv_usec -= 1000000 * nsec;
    y2.tv_sec += nsec;
  }
  if (x->tv_usec - y2.tv_usec > 1000000) {
    int nsec = (x->tv_usec - y2.tv_usec) / 1000000;
    y2.tv_usec += 1000000 * nsec;
    y2.tv_sec -= nsec;
  }

  /* Compute the time remaining to wait.
     tv_usec is certainly positive. */
  result->tv_sec = x->tv_sec - y2.tv_sec;
  result->tv_usec = x->tv_usec - y2.tv_usec;

  /* Return 1 if result is negative. */
  return x->tv_sec < y2.tv_sec;
}

void
add_timeout(struct timeval a, int b, struct timespec *result, 
	    struct timeval *result_tv) {
  // convert to millisec, add timeout, convert back
  long double msec = (a.tv_sec*1000.0 + a.tv_usec/1000.0)+b*1.0;
  result->tv_sec = (long) (msec/1000);
  result->tv_nsec = 
    ((long) ((msec-((long double) result->tv_sec*1000.0))*1000))*1000;
  result_tv->tv_sec = result->tv_sec;
  result_tv->tv_usec = result->tv_nsec/1000;
}

int
rpcc::call1(sockaddr_in dst, unsigned int proc,
            const marshall &req, unmarshall &rep, TO to)
{

  assert(pthread_mutex_lock(&m) == 0);
  unsigned int myxid = xid++;
  marshall m1;

  caller ca(myxid, &rep, dst.sin_addr.s_addr, dst.sin_port,
#ifdef VIVALDI
            (proc & rpc_const::VIVALDI_MASK)?vivaldi::gettime():0.0
#else
            0.0
#endif
            );
  calls[myxid] = &ca;
  assert(pthread_mutex_unlock(&m) == 0);

  m1 << proc << myxid << req.str();

  struct timeval first;
  gettimeofday(&first, NULL);
  struct timeval last = first;
  int initial_rto = 250; // initial timeout (msec)
  int rto = initial_rto;
  int next_rto = rto;

  struct timespec deadline;
  struct timeval deadline_tv;
  if(to.to != -1) {
    add_timeout(first, to.to, &deadline, &deadline_tv);
  }

  pthread_mutex_lock(&ca.m);
  struct timeval now, diff;
  gettimeofday(&now, NULL);
  while(1){
    if(ca.done)
      break;
    assert(timeval_subtract(&diff, &now, &last) == 0);
    long double diff_msec = diff.tv_sec*1000.0 + diff.tv_usec/1000.0;
    if(diff_msec >= rto || next_rto == initial_rto){
      rto = next_rto;
      if(rto != initial_rto)
        fprintf(stderr, "rpcc::call retransmit proc %x xid %d %s:%d\n", 
                proc, myxid,
                inet_ntoa(dst.sin_addr),
                ntohs(dst.sin_port));
      int rec_to;
      chan.send(dst, m1.str(), rec_to);
      if(rec_to > rto)
        rto = rec_to;
      gettimeofday(&last, NULL);
      // increase rxmit timer for next time
      next_rto *= 2;
      if(next_rto > 128000)
	next_rto = 128000;
    }

    // rexmit deadline
    struct timespec my_deadline;
    struct timeval my_deadline_tv;
    add_timeout(last, rto, &my_deadline, &my_deadline_tv);

    // my next deadline is either for rxmit or timeout
    if(to.to != -1 && 
       timeval_subtract(&diff, &deadline_tv, &my_deadline_tv) > 0) {
      my_deadline = deadline;
      my_deadline_tv = deadline_tv;
    }

    assert(pthread_mutex_lock(&_timeout_lock) == 0);
    struct timeval nt_tv;
    nt_tv.tv_sec = _next_timeout.tv_sec;
    nt_tv.tv_usec = _next_timeout.tv_nsec/1000;
    if(_next_timeout.tv_sec == 0 || 
       timeval_subtract(&diff, &my_deadline_tv, &nt_tv) > 0){
      _next_timeout = my_deadline;
      pthread_cond_signal(&_timeout_cond);
    }
    assert(pthread_mutex_unlock(&_timeout_lock) == 0);

    // wait for reply or timeout
    assert(pthread_cond_wait(&ca.c, &ca.m) == 0);

    // user-specified timeout occurred
    gettimeofday(&now, NULL);
    if(!ca.done && to.to != -1){
      if(timeval_subtract(&diff, &deadline_tv, &now) > 0)
	break;
    }

  }
  int intret = ca.done ? ca.intret : -1;
  pthread_mutex_unlock(&ca.m);

  assert(pthread_mutex_lock(&m) == 0);
  calls.erase(myxid);
  assert(pthread_mutex_unlock(&m) == 0);

  return intret;
}

// listen for replies, hand to xid's waiting rpcc::call().
void
rpcc::chan_loop()
{
  while(1){
    std::string s = chan.recv();
    unmarshall rep(s);
    got_reply(rep);
  }
}

// periodically wakes up all callers so they can think
// about retransmitting.
void
rpcc::clock_loop()
{
  while(1){

    // if there are no timeouts, wake until there is one
    assert(pthread_mutex_lock(&_timeout_lock) == 0);
    bool signalled = false;
    if(_next_timeout.tv_sec == 0){
      pthread_cond_wait(&_timeout_cond, &_timeout_lock);
      signalled = true;
    }else{
      signalled = 
	(pthread_cond_timedwait(&_timeout_cond, &_timeout_lock, &_next_timeout)
	 != ETIMEDOUT);
    }

    // if we were signalled to be woken up, that means our timeout value
    // was set to something new, so we just need to wait again, and not
    // bother waking everyone up
    if(!signalled)
      _next_timeout.tv_sec = 0;
    assert(pthread_mutex_unlock(&_timeout_lock) == 0);
    if(signalled)
      continue;

    assert(pthread_mutex_lock(&m) == 0);
    std::map<int,caller*>::iterator iter;
    for(iter = calls.begin(); iter != calls.end(); iter++){
      caller *ca = iter->second;
      assert(pthread_mutex_lock(&ca->m) == 0);
      assert(pthread_cond_broadcast(&ca->c) == 0);
      assert(pthread_mutex_unlock(&ca->m) == 0);
    }
    assert(pthread_mutex_unlock(&m) == 0);
  }
}

void
rpcc::got_reply(unmarshall &rep)
{
  int xid, intret;
  rep >> xid;
  if(!rep.ok())
    return;

  assert(pthread_mutex_lock(&m) == 0);

  if(calls.count(xid) < 1){
    assert(pthread_mutex_unlock(&m) == 0);
#if 0
    fprintf(stderr, "rpcc: no call for reply xid %d\n", xid);
#endif
    return;
  }
        
  caller *ca = calls[xid];
  
  assert(pthread_mutex_lock(&ca->m) == 0);

#ifdef VIVALDI
  if (ca->senttime > 0.0) {
	 Coord c;
	 rep >> c;
	 assert(_vivaldi);
	 double now = vivaldi::gettime();
	 _vivaldi->update_other_coords(ca->other_ip, ca->other_port, c);
	 _vivaldi->update_other_lat (ca->other_ip, now - ca->senttime);
	 _vivaldi->update_coords(c, now - ca->senttime);
  }
#endif
  rep >> intret;
  ca->un->str(rep.istr());
  ca->intret = intret;
  ca->done = 1;
  assert(pthread_cond_broadcast(&ca->c) == 0);
  assert(pthread_mutex_unlock(&ca->m) == 0);
  
  // Wait until down here to unlock the global lock.  This avoids the
  // case where got_reply() gets the caller from the map and locks ca->m, 
  // then call1() erases the caller from the map and tries to destroy the
  // caller object before ca->m is unlocked, causing disaster.
  assert(pthread_mutex_unlock(&m) == 0);

}

rpcs::rpcs(unsigned int port)
  : chan(port), lossy(false), lossy_percent(5), _vivaldi(NULL), counting(0)
{
  assert(pthread_mutex_init(&m, 0) == 0);

  char *loss_env = getenv("RPC_LOSSY");
  if(loss_env != NULL){
    lossy_percent = atoi(loss_env);
    if(lossy_percent > 0){
      setlossy(true);
    }
  }

  char *count_env = getenv("RPC_COUNT");
  if(count_env != NULL){
    counting = atoi(count_env);
  }

  if(method_thread(this, true, &rpcs::loop) == 0){
    perror("rpcs::rpcs pthread_create");
    exit(1);
  }
}

rpcs::~rpcs()
{
}

void
rpcs::setlossy(bool x)
{
  lossy = x;
  //chan.setlossy(x);
}

handler::handler()
{
}

void
rpcs::reg1(unsigned int proc, handler *h)
{
  assert(pthread_mutex_lock(&m) == 0);
  assert(procs.count(proc) == 0);
  procs[proc] = h;
  assert(procs.count(proc) >= 1);
  assert(pthread_mutex_unlock(&m) == 0);
}

void
rpcs::dispatch(junk *j)
{
  std::string xreq = j->s;
  schan::chan_key key = j->key;
  delete j;
  unmarshall req(xreq);
  unsigned int proc = req.i32();
  unsigned int xid = req.i32();
  unmarshall args(req.istr());

  assert(pthread_mutex_lock(&m) == 0);

  if(!req.ok() || procs.count(proc) < 1){
    assert(pthread_mutex_unlock(&m) == 0);
    fprintf(stderr, "bad proc %x req ok? %d procs.count %lu\n",
            proc, req.ok()?1:0, procs.count(proc));
    return;
  }

  handler *h = procs[proc];

  assert(pthread_mutex_unlock(&m) == 0);

  if(lossy){
    if((random() % 100) < lossy_percent)
      return; // drop request
    if((random() % 100) < 10)
      sleep(random() % 10); // delay request
    if((random() % 100) < lossy_percent)
      dispatch(new junk(xreq, key)); // duplicate request
  }

  marshall rep;

  // try/catch in order to let handler use throw
  // to avoid returning anything.
  try {
    int ret = h->fn(args, rep);

    marshall rep1;
    rep1 << xid;
#ifdef VIVALDI
    if (proc & rpc_const::VIVALDI_MASK) {
      assert(_vivaldi);
      rep1 << _vivaldi->my_coords();
    }
#endif
    rep1 << ret;
    rep1 << rep.str();

    if(lossy){
      if((random() % 100) < lossy_percent)
        return; // drop reply
      if((random() % 100) < 10)
        sleep(random() % 10); // delay reply
      if((random() % 100) < lossy_percent)
        chan.send(rep1.str(), key); // duplicate reply
    }

    chan.send(rep1.str(), key);
  } catch(...) {
    ;
  }
}

rpcs::junk::junk(std::string xs, schan::chan_key xkey)
  : s(xs), key(xkey)
{ }

void
rpcs::loop()
{
  while(1){
    std::string req;
    schan::chan_key key;
    chan.recv(req, key);

    if(req.size() < 8)
      continue;

    junk *j = new junk(req, key);

    if( counting > 0 ) {
      unmarshall ureq(req);
      unsigned int proc = ureq.i32();
      counts[proc]++;
      counting--;
      if( counting == 0 ) {
	std::map<int, int>::iterator i;
	printf( "RPC STATS: " );
	for( i = counts.begin(); i != counts.end(); i++ ) {
	  printf( "%x:%d ", i->first, i->second );
	}
	printf("\n");
	char *count_env = getenv("RPC_COUNT");
	if(count_env != NULL){
	  counting = atoi(count_env);
	}
      }
    }

    if(method_thread(this, true, &rpcs::dispatch, j) == 0){
      perror("rpcs::loop pthread_create");
      exit(1);
    }
  }
}

void
marshall::rawbyte(unsigned x)
{
  s.put((unsigned char) x);
}

void
marshall::rawbytes(const char *p, int n)
{
  s.write(p, n);
}

marshall &
operator<<(marshall &m, unsigned char x)
{
  m.rawbyte(x);
  return m;
}

marshall &
operator<<(marshall &m, char x)
{
  m << (unsigned char) x;
  return m;
}


marshall &
operator<<(marshall &m, unsigned short x)
{
  m.rawbyte(x & 0xff);
  m.rawbyte((x >> 8) & 0xff);
  return m;
}

marshall &
operator<<(marshall &m, short x)
{
  m << (unsigned short) x;
  return m;
}

marshall &
operator<<(marshall &m, unsigned int x)
{
  m.rawbyte(x & 0xff);
  m.rawbyte((x >> 8) & 0xff);
  m.rawbyte((x >> 16) & 0xff);
  m.rawbyte((x >> 24) & 0xff);
  return m;
}

marshall &
operator<<(marshall &m, unsigned long x)
{
  m.rawbyte(x & 0xff);
  m.rawbyte((x >> 8) & 0xff);
  m.rawbyte((x >> 16) & 0xff);
  m.rawbyte((x >> 24) & 0xff);
  return m;
}

marshall &
operator<<(marshall &m, int x)
{
  m << (unsigned int) x;
  return m;
}

marshall &
operator<<(marshall &m, const std::string &s)
{
  m << (unsigned int) s.size();
  m.rawbytes(s.data(), s.size());
  return m;
}

marshall &
operator<<(marshall &m, unsigned long long x)
{
  m << (unsigned int) x;
  m << (unsigned int) (x >> 32);
  return m;
}


bool
unmarshall::okdone()
{
  if(ok() && ((int)s.tellg() == (int)s.str().size())){
    return true;
  } else {
    // an RPC request or reply was too short or too long.
#if 0
    unsigned int *p = (unsigned int *) s.str().data();
    fprintf(stderr, "okdone failed len=%d %x %x %x\n",
            s.str().size(), p[0], p[1], p[2]);
#endif
    return false;
  }
}

unsigned int
unmarshall::rawbyte()
{
  char c = 0;
  if(!s.get(c))
    _ok = false;
  return c;
}

unmarshall &
operator>>(unmarshall &u, unsigned char &x)
{
  x = (unsigned char) u.rawbyte() ;
  return u;
}

unmarshall &
operator>>(unmarshall &u, char &x)
{
  x = (char) u.rawbyte();
  return u;
}


unmarshall &
operator>>(unmarshall &u, unsigned short &x)
{
  x = u.rawbyte() & 0xff;
  x |= (u.rawbyte() & 0xff) << 8;
  return u;
}

unmarshall &
operator>>(unmarshall &u, short &x)
{
  x = u.rawbyte() & 0xff;
  x |= (u.rawbyte() & 0xff) << 8;
  return u;
}

unmarshall &
operator>>(unmarshall &u, unsigned int &x)
{
  x = u.rawbyte() & 0xff;
  x |= (u.rawbyte() & 0xff) << 8;
  x |= (u.rawbyte() & 0xff) << 16;
  x |= (u.rawbyte() & 0xff) << 24;
  return u;
}

unmarshall &
operator>>(unmarshall &u, unsigned long &x)
{
  x = u.rawbyte() & 0xff;
  x |= (u.rawbyte() & 0xff) << 8;
  x |= (u.rawbyte() & 0xff) << 16;
  x |= (u.rawbyte() & 0xff) << 24;
  return u;
}

unmarshall &
operator>>(unmarshall &u, int &x)
{
  x = u.rawbyte() & 0xff;
  x |= (u.rawbyte() & 0xff) << 8;
  x |= (u.rawbyte() & 0xff) << 16;
  x |= (u.rawbyte() & 0xff) << 24;
  return u;
}

unmarshall &
operator>>(unmarshall &u, unsigned long long &x)
{
  unsigned int a, b;
  u >> a;
  u >> b;
  x = a | ((unsigned long long) b << 32);
  return u;
}

unmarshall &
operator>>(unmarshall &u, std::string &s)
{
  unsigned sz;
  u >> sz;
  if(u.ok())
    s = u.rawbytes(sz);
  return u;
}

std::string
unmarshall::rawbytes(unsigned int n)
{
  char *p = new char [n];
  unsigned nn = s.readsome(p, n);
  if(nn < n)
    _ok = false;
  std::string s(p, n);
  delete [] p;
  return s;
}

unsigned int
unmarshall::i32()
{
  unsigned int x;
  (*this) >> x;
  return x;
}

unsigned long long
unmarshall::i64()
{
  unsigned long long x;
  (*this) >> x;
  return x;
}

std::string
unmarshall::istr()
{
  std::string s;
  (*this) >> s;
  return s;
}

void 
make_sockaddr(const char *hostandport, struct sockaddr_in *dst){

  char host[200];
  char *localhost = "127.0.0.1";
  const char *port = index(hostandport, ':');
  if(port == NULL){
    memcpy(host, localhost, strlen(localhost)+1);
    port = hostandport;
  }else{
    memcpy(host, hostandport, port-hostandport);
    host[port-hostandport] = '\0';
    port++;
  }

  make_sockaddr(host, port, dst);

}

void 
make_sockaddr(const char *host, const char *port, struct sockaddr_in *dst){

  in_addr_t a;

  bzero(dst, sizeof(*dst));
  dst->sin_family = AF_INET;

  a = inet_addr(host);
  if(a != INADDR_NONE){
    dst->sin_addr.s_addr = a;
  } else {
    struct hostent *hp = gethostbyname(host);
    if(hp == 0 || hp->h_length != 4){
      fprintf(stderr, "cannot find host name %s\n", host);
      exit(1);
    }
    dst->sin_addr.s_addr = ((struct in_addr *)(hp->h_addr))->s_addr;
  }
  dst->sin_port = htons(atoi(port));

}
