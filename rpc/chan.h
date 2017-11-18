#ifndef chan_h
#define chan_h 1

// channel abstraction that sits below rpc.
// the main goal is to be able to send RPCs over
// tcp to get good wide-area performance.
// this layer understands about connections and
// multiplexing many send()s simultaneously over a connection.

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string>
#include <map>
#include "fifo.h"

class tcpchan;

// for rpcs
class schan {
 private:
  struct inbuf;

 public:
  schan(int port);

  class chan_key {
  public:
    chan_key() : key(0) { sin.sin_family = 0; }
  private:
    chan_key(sockaddr_in xsin) : sin(xsin), key(0) { }
    chan_key(int xkey) : key(xkey) { sin.sin_family = 0; }
    sockaddr_in sin;
    int key;
    friend class schan;
    friend class inbuf;
    // really what I want is public within schan, nameable
    // outside schan, but all fields private outside schan.
  };

  void recv(std::string &pdu, chan_key &key);
  void send(std::string pdu, chan_key key);
  void setlossy(bool); // for debugging

 private:
  int udp;
  int tcp;
  int next_key;
  bool lossy;
  int lossy_percent;

  // map key to which tcp socket to send rpc reply on.
  std::map<int, tcpchan*> tcpchans;
  pthread_mutex_t tcpchans_m;
  pthread_cond_t tcpchans_c;

  // input queue. fed by a loop1() per tcpchan.
  // read & waited for by rpc's calls to recv().
  struct inbuf {
    inbuf(std::string xs, chan_key xkey) : s(xs), key(xkey) { }
    inbuf() { }
    std::string s;
    chan_key key;
  };
  fifo<inbuf> inq;
  
  void tcp_loop();
  void tcp_loop1(int s);
  void udp_loop();
  void send_tcp(int key, std::string pdu);
  int find_him(sockaddr_in him);
};

// for rpcc
class cchan {
 public:
  cchan();

  void send(sockaddr_in sin, std::string pdu,
            int &timeout_recommendation);
  std::string recv();

 private:
  int udp;
  short my_port;
  std::map<sockaddr_in, tcpchan *> tcpchans;
  pthread_mutex_t tcpchans_m;
  time_t rate_t;
  int rate_n;

  fifo<std::string> inq;

  void send_udp(sockaddr_in sin, std::string pdu);
  void send_tcp(sockaddr_in sin, std::string pdu);
  void tcp_loop(sockaddr_in sin, tcpchan *ch);
  void udp_loop();
  void setup_tcp(sockaddr_in sin);
};

// internal. one tcp connection (client or server end).
// multiplexes big send()s, un-multiplexes on recv().
class tcpchan {
 public:
  tcpchan(sockaddr_in dst, short my_port);
  tcpchan(int sock);
  ~tcpchan();

  void send(std::string);
  std::string recv();
  bool dead() { return isdead; }
  void lose();
  sockaddr_in him() { return _him; }

 private:
  int s;
  pthread_t th;
  bool isdead; // tell owning thread to delete
  sockaddr_in _him; // client's UDP sockaddr

  // machinery for recv() to wait for connect() to finish
  bool isconnecting;
  pthread_mutex_t connecting_m;
  pthread_cond_t connecting_c;

  fifo<std::string> outq;

  int xread(void *p, unsigned int n, bool die_on_error = true);
  void do_connect(sockaddr_in dst, short my_port);
  void output_loop();
  void send1(std::string);
  void die();
};

#endif
