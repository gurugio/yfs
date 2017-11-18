#ifndef rpc_h
#define rpc_h

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <map>
#include "marshall.h"
#include "chan.h"

class rpc_const {
  public:
  static const int VIVALDI_MASK=0x40000000;
};
// rpc client endpoint.
// manages a socket and an xid space.
// threaded: multiple threads can be sending RPCs,
// and this code gives each reply to the right thread.
class rpcc {
 private:
  unsigned int xid;
  cchan chan;
  class vivaldi *_vivaldi;

  // clock loop data for tight timeouts
  pthread_mutex_t _timeout_lock;
  pthread_cond_t _timeout_cond;
  struct timespec _next_timeout;

  // map xid of awaited reply to waiting thread in call().
  struct caller {
    caller(int xxid, unmarshall *un, uint32_t ip, uint16_t port, double when = 0);
    ~caller();
    int xid;
    unmarshall *un;
    int intret;
    bool done;
    pthread_cond_t c;
    pthread_mutex_t m;
    double senttime;
    uint32_t other_ip;
    uint16_t other_port;
  };
  std::map<int, caller*> calls;
  pthread_mutex_t m; // protect insert/delete to calls[]

  void chan_loop();
  void clock_loop();
  void got_reply(unmarshall &rep);

 public:
  rpcc();
  ~rpcc();

  void set_vivaldi(vivaldi *v) { _vivaldi = v; }

  // hack to allow optional timeout argument to call().
  struct TO {
    int to;
  };
  static const TO to_inf;
  static TO to(int x) { TO t; t.to = x; return t; }

  int call1(sockaddr_in dst, unsigned int proc,
            const marshall &req, unmarshall &rep, TO to);

  template<class R>
    int call(sockaddr_in dst, unsigned int proc, R & r, TO to = to_inf);
  template<class R, class A1>
    int call(sockaddr_in dst, unsigned int proc, const A1 & a1,
             R & r, TO to = to_inf);
  template<class R, class A1, class A2>
    int call(sockaddr_in dst, unsigned int proc, const A1 & a1,
             const A2 & a2, R & r, TO to = to_inf);
  template<class R, class A1, class A2, class A3>
    int call(sockaddr_in dst, unsigned int proc, const A1 & a1,
             const A2 & a2, const A3 & a3, R & r, TO to = to_inf);
  template<class R, class A1, class A2, class A3, class A4>
    int call(sockaddr_in dst, unsigned int proc, const A1 & a1,
             const A2 & a2, const A3 & a3, const A4 & a4, R & r, 
	     TO to = to_inf);
  template<class R, class A1, class A2, class A3, class A4, class A5>
    int call(sockaddr_in dst, unsigned int proc, const A1 & a1,
             const A2 & a2, const A3 & a3, const A4 & a4, const A5 & a5, 
	     R & r, TO to = to_inf);
};

template<class R> int
rpcc::call(sockaddr_in dst, unsigned int proc, R & r, TO to)
{
  marshall m;
  unmarshall u;
  int intret = call1(dst, proc, m, u, to);
  u >> r;
  if(u.okdone() != true)
    return -1;
  return intret;
}

template<class R, class A1> int
rpcc::call(sockaddr_in dst, unsigned int proc, const A1 & a1, R & r, TO to)
{
  marshall m;
  unmarshall u;
  m << a1;
  int intret = call1(dst, proc, m, u, to);
  u >> r;
  if(u.okdone() != true)
    return -1;
  return intret;
}

template<class R, class A1, class A2> int
rpcc::call(sockaddr_in dst, unsigned int proc, const A1 & a1, const A2 & a2,
           R & r, TO to)
{
  marshall m;
  unmarshall u;
  m << a1;
  m << a2;
  int intret = call1(dst, proc, m, u, to);
  u >> r;
  if(u.okdone() != true)
    return -1;
  return intret;
}

template<class R, class A1, class A2, class A3> int
rpcc::call(sockaddr_in dst, unsigned int proc, const A1 & a1, const A2 & a2,
           const A3 & a3, R & r, TO to)
{
  marshall m;
  unmarshall u;
  m << a1;
  m << a2;
  m << a3;
  int intret = call1(dst, proc, m, u, to);
  u >> r;
  if(u.okdone() != true)
    return -1;
  return intret;
}

template<class R, class A1, class A2, class A3, class A4> int
rpcc::call(sockaddr_in dst, unsigned int proc, const A1 & a1, const A2 & a2,
           const A3 & a3, const A4 & a4, R & r, TO to)
{
  marshall m;
  unmarshall u;
  m << a1;
  m << a2;
  m << a3;
  m << a4;
  int intret = call1(dst, proc, m, u, to);
  u >> r;
  if(u.okdone() != true)
    return -1;
  return intret;
}

template<class R, class A1, class A2, class A3, class A4, class A5> int
rpcc::call(sockaddr_in dst, unsigned int proc, const A1 & a1, const A2 & a2,
           const A3 & a3, const A4 & a4, const A5 & a5, R & r, TO to)
{
  marshall m;
  unmarshall u;
  m << a1;
  m << a2;
  m << a3;
  m << a4;
  m << a5;
  int intret = call1(dst, proc, m, u, to);
  u >> r;
  if(u.okdone() != true)
    return -1;
  return intret;
}

class handler {
 public:
  handler();
  virtual ~handler() { }
  virtual int fn(unmarshall &, marshall &) = 0;
};

// rpc server endpoint.
class rpcs {
 private:
  schan chan;
  bool lossy; // debug: drop some requests and replies
  int lossy_percent; // percentage of packets to drop if lossy is true
  class vivaldi *_vivaldi;

  // counting
  int counting;
  std::map<int, int> counts;

  // map proc # to function
  std::map<int, handler *> procs;
  pthread_mutex_t m; // protect insert/delete to procs[]

  void loop();

  //void dispatch(std::string, sockaddr_in); // -> compiler error
  struct junk {
    junk(std::string, schan::chan_key);
    std::string s;
    schan::chan_key key;
  };
  void dispatch(junk *);

 public:
  rpcs(unsigned int port);
  ~rpcs();

  void set_vivaldi(vivaldi *v) { _vivaldi = v; }

  // internal handler registration
  void reg1(unsigned int proc, handler *);

  // register a handler
  template<class S, class A1, class R>
    void reg(unsigned int proc, S*, int (S::*meth)(const A1 a1, R & r));
  template<class S, class A1, class A2, class R>
    void reg(unsigned int proc, S*, int (S::*meth)(const A1 a1, const A2, 
						   R & r));
  template<class S, class A1, class A2, class A3, class R>
    void reg(unsigned int proc, S*, int (S::*meth)(const A1, const A2, 
                                          const A3, R & r));
  template<class S, class A1, class A2, class A3, class A4, class R>
    void reg(unsigned int proc, S*, int (S::*meth)(const A1, const A2, 
                                          const A3, const A4, R & r));
  template<class S, class A1, class A2, class A3, class A4, class A5, class R>
    void reg(unsigned int proc, S*, int (S::*meth)(const A1, const A2, 
                                          const A3, const A4, const A5, 
					  R & r));

  void setlossy(bool);
};

template<class S, class A1, class R> void
rpcs::reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, R & r))
{
  class h1 : public handler {
  private:
    S * sob;
    int (S::*meth)(const A1 a1, R & r);
  public:
    h1(S *xsob, int (S::*xmeth)(const A1 a1, R & r))
      : sob(xsob), meth(xmeth) { }
    int fn(unmarshall &args, marshall &ret) {
      A1 a1;
      R r;
      args >> a1;
      if(!args.okdone())
        return -1;
      int b = (sob->*meth)(a1, r);
      ret << r;
      return b;
    }
  };
  reg1(proc, new h1(sob, meth));
}

template<class S, class A1, class A2, class R> void
rpcs::reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, 
						   R & r))
{
  class h1 : public handler {
  private:
    S * sob;
    int (S::*meth)(const A1 a1, const A2 a2, R & r);
  public:
    h1(S *xsob, int (S::*xmeth)(const A1 a1, const A2 a2, R & r))
      : sob(xsob), meth(xmeth) { }
    int fn(unmarshall &args, marshall &ret) {
      A1 a1;
      A2 a2;
      R r;
      args >> a1;
      args >> a2;
      if(!args.okdone())
        return -1;
      int b = (sob->*meth)(a1, a2, r);
      ret << r;
      return b;
    }
  };
  reg1(proc, new h1(sob, meth));
}

template<class S, class A1, class A2, class A3, class R> void
rpcs::reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, 
                                          const A3 a3, R & r))
{
  class h1 : public handler {
  private:
    S * sob;
    int (S::*meth)(const A1 a1, const A2 a2, const A3 a3, R & r);
  public:
    h1(S *xsob, int (S::*xmeth)(const A1 a1, const A2 a2, const A3 a3, R & r))
      : sob(xsob), meth(xmeth) { }
    int fn(unmarshall &args, marshall &ret) {
      A1 a1;
      A2 a2;
      A3 a3;
      R r;
      args >> a1;
      args >> a2;
      args >> a3;
      if(!args.okdone())
        return -1;
      int b = (sob->*meth)(a1, a2, a3, r);
      ret << r;
      return b;
    }
  };
  reg1(proc, new h1(sob, meth));
}

template<class S, class A1, class A2, class A3, class A4, class R> void
rpcs::reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, 
                                          const A3 a3, const A4 a4, R & r))
{
  class h1 : public handler {
  private:
    S * sob;
    int (S::*meth)(const A1 a1, const A2 a2, const A3 a3, const A4 a4, R & r);
  public:
    h1(S *xsob, int (S::*xmeth)(const A1 a1, const A2 a2, const A3 a3, 
				const A4 a4, R & r))
      : sob(xsob), meth(xmeth) { }
    int fn(unmarshall &args, marshall &ret) {
      A1 a1;
      A2 a2;
      A3 a3;
      A4 a4;
      R r;
      args >> a1;
      args >> a2;
      args >> a3;
      args >> a4;
      if(!args.okdone())
        return -1;
      int b = (sob->*meth)(a1, a2, a3, a4, r);
      ret << r;
      return b;
    }
  };
  reg1(proc, new h1(sob, meth));
}

template<class S, class A1, class A2, class A3, class A4, class A5, class R> void
rpcs::reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, 
                                          const A3 a3, const A4 a4, 
					  const A5 a5, R & r))
{
  class h1 : public handler {
  private:
    S * sob;
    int (S::*meth)(const A1 a1, const A2 a2, const A3 a3, const A4 a4, 
		   const A5 a5, R & r);
  public:
    h1(S *xsob, int (S::*xmeth)(const A1 a1, const A2 a2, const A3 a3, 
				const A4 a4, const A5 a5, R & r))
      : sob(xsob), meth(xmeth) { }
    int fn(unmarshall &args, marshall &ret) {
      A1 a1;
      A2 a2;
      A3 a3;
      A4 a4;
      A5 a5;
      R r;
      args >> a1;
      args >> a2;
      args >> a3;
      args >> a4;
      args >> a5;
      if(!args.okdone())
        return -1;
      int b = (sob->*meth)(a1, a2, a3, a4, a5, r);
      ret << r;
      return b;
    }
  };
  reg1(proc, new h1(sob, meth));
}

void make_sockaddr(const char *hostandport, struct sockaddr_in *dst);
void make_sockaddr(const char *host, const char *port, 
		   struct sockaddr_in *dst);

#endif
