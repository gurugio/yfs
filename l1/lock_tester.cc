//
// Lock server tester
//
#include <stdio.h>
#include <unistd.h>

#include "lock_protocol.h"
#include "lock_client.h"
#include "rpc/rpc.h"
#include <arpa/inet.h>

int nt = 10;
struct sockaddr_in dst;
lock_client **lc = new lock_client * [nt];
std::string a = std::string("a");
std::string b = std::string("b");
std::string c = std::string("c");

// check_grant() and check_release() check that the lock server
// doesn't grant the same lock to both clients.
// it assumes that lock names are distinct in the first byte.
int count[256];
pthread_mutex_t count_mutex;

// convert a bit-string to a hex string for debug printouts.
std::string
hex(std::string s)
{
  char buf[64];
  unsigned int len = s.length();
  const char *p = s.c_str();
  unsigned int i;

  buf[0] = '\0';
  for(i = 0; i < len && i*2+1 < sizeof(buf); i++){
    sprintf(buf + (i * 2), "%02x", p[i] & 0xff);
  }
  
  return std::string(buf);
}

void
check_grant(std::string name)
{
  pthread_mutex_lock(&count_mutex);
  int x = name.c_str()[0] & 0xff;
  if(count[x] != 0){
    fprintf(stderr, "error: server granted %s %d-times\n",
	    hex(name).c_str(), count[x]);
    exit(1);
  }
  count[x] += 1;
  pthread_mutex_unlock(&count_mutex);
}

void
check_release(std::string name)
{
  pthread_mutex_lock(&count_mutex);
  int x = name.c_str()[0] & 0xff;
  if(count[x] != 1){
    fprintf(stderr, "error: client released un-held lock %s\n", 
	    hex(name).c_str());
    exit(1);
  }
  count[x] -= 1;
  pthread_mutex_unlock(&count_mutex);
}

void
test1(void)
{
    printf ("acquire a release a acquire a release a\n");
    lc[0]->acquire(a);
    check_grant(a);
    lc[0]->release(a);
    check_release(a);
    lc[0]->acquire(a);
    check_grant(a);
    lc[0]->release(a);
    check_release(a);

    printf ("acquire a acquire b release b releasea \n");
    lc[0]->acquire(a);
    check_grant(a);
    lc[0]->acquire(b);
    check_grant(b);
    lc[0]->release(b);
    check_release(b);
    lc[0]->release(a);
    check_release(a);
}

void *
test2(void *x) 
{
  int i = * (int *) x;

  printf ("test2: client %d acquire a release a\n", i);
  lc[i]->acquire(a);
  printf ("test2: client %d sleep\n", i);
  check_grant(a);
  sleep(1);
  printf ("test2: client %d release\n", i);
  check_release(a);
  lc[i]->release(a);
  return 0;
}

void *
test3(void *x)
{
  int i = * (int *) x;

  printf ("test3: client %d acquire a release a concurrent\n", i);
  for (int j = 0; j < 10; j++) {
    lc[i]->acquire(a);
    check_grant(a);
    printf ("test3: client %d got lock\n", i);
    check_release(a);
    lc[i]->release(a);
  }
  return 0;
}

void *
test4(void *x)
{
  int i = * (int *) x;

  printf ("test4: client %d acquire a release a concurrent; same clnt\n", i);
  for (int j = 0; j < 10; j++) {
    lc[0]->acquire(a);
    check_grant(a);
    printf ("test4: client %d got lock\n", i);
    check_release(a);
    lc[0]->release(a);
  }
  return 0;
}

void *
test5(void *x)
{
  int i = * (int *) x;

  printf ("test5: client %d acquire a release a concurrent; same and diff clnt\n", i);
  for (int j = 0; j < 10; j++) {
    if (i < 5)  lc[0]->acquire(a);
    else  lc[1]->acquire(a);
    check_grant(a);
    printf ("test5: client %d got lock\n", i);
    check_release(a);
    if (i < 5) lc[0]->release(a);
    else lc[1]->release(a);
  }
  return 0;
}

int
main(int argc, char *argv[])
{
    int r;
    pthread_t th[nt];

    setvbuf(stdout, NULL, _IONBF, 0);

    if(argc != 2 && argc != 3){
      fprintf(stderr, "Usage: %s [host:]port [test]\n", argv[0]);
      exit(1);
    }
    
    make_sockaddr(argv[1], &dst);

    int test = 0;
    if(argc == 3) {
      test = atoi(argv[2]);
      if(test < 1 || test > 5){
	printf("Test number must be between 1 and 5\n");
	exit(1);
      }
    }

    assert(pthread_mutex_init(&count_mutex, NULL) == 0);

    printf("simple lock client\n");
    for (int i = 0; i < nt; i++) lc[i] = new lock_client(dst);

    if(!test || test == 1){
      test1();
    }

    if(!test || test == 2){
      // test2
      for (int i = 0; i < nt; i++) {
	int *a = new int (i);
	r = pthread_create(&th[i], NULL, test2, (void *) a);
	assert (r == 0);
      }
      for (int i = 0; i < nt; i++) {
	pthread_join(th[i], NULL);
      }
    }

    if(!test || test == 3){
      printf("test 3\n");
      
      // test3
      for (int i = 0; i < nt; i++) {
	int *a = new int (i);
	r = pthread_create(&th[i], NULL, test3, (void *) a);
	assert (r == 0);
      }
      for (int i = 0; i < nt; i++) {
	pthread_join(th[i], NULL);
      }
    }

    if(!test || test == 4){
      printf("test 4\n");
      
      // test 4
      for (int i = 0; i < 2; i++) {
	int *a = new int (i);
	r = pthread_create(&th[i], NULL, test4, (void *) a);
	assert (r == 0);
      }
      for (int i = 0; i < 2; i++) {
	pthread_join(th[i], NULL);
      }
    }

    if(!test || test == 5){
      printf("test 5\n");
      
      // test 5
      
      for (int i = 0; i < 10; i++) {
	int *a = new int (i);
	r = pthread_create(&th[i], NULL, test5, (void *) a);
	assert (r == 0);
      }
      for (int i = 0; i < 10; i++) {
	pthread_join(th[i], NULL);
      }
    }

    printf ("%s: passed all tests successfully\n", argv[0]);

}
