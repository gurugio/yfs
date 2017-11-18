LAB=2
SOL=2
RPC=../rpc
CFLAGS =  -g -MD -Wall -I$(RPC) -DLAB=$(LAB) -DSOL=$(SOL)
CXX = g++

LOCK_SERVER = lock_server.o lock_smain.o rpc.o host.o chan.o
LOCK_TESTER = lock_tester.o lock_client.o rpc.o host.o chan.o
LOCK_DEMO = lock_demo.o lock_client.o rpc.o host.o chan.o

YFS_SERVER = yfs_smain.o yfs_server.o extent_client.o rpc.o host.o chan.o
EXTEND_SERVER = extent_server.o extent_smain.o rpc.o host.o chan.o
FUSE2YFS = fuse.o yfs_client.o rpc.o host.o chan.o

FUSEFLAGS = -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=25 -I/usr/local/include/fuse
MACFLAGS = -D__FreeBSD__=10

all: lab1

lab1: lock_server lock_tester lock_demo

extent_server : $(EXTEND_SERVER)
	$(CXX) $(CFLAGS) -o extent_server $(EXTEND_SERVER) -lpthread

yfs_server : $(YFS_SERVER)
	$(CXX) $(CFLAGS) -o yfs_server $(YFS_SERVER) -lpthread

fuse2yfs : $(FUSE2YFS)
	if [ `uname` = "linux" -o `uname` = "Linux" -o `uname` = "linux-gnu" ]; then \
		$(CXX) $(CFLAGS) -o fuse2yfs $(FUSE2YFS) -L/usr/local/lib -lfuse -lpthread -lrt -ldl; \
	else \
		$(CXX) $(CFLAGS) -o fuse2yfs $(FUSE2YFS) -L/usr/local/lib -lfuse -lpthread; \
	fi

LS=$(LOCK_SERVER)
lock_server : $(LS)
	if [ `uname` = "SunOS" ]; then \
		$(CXX) $(CFLAGS) -o lock_server $(LS) -lpthread -lxnet; \
	else \
		$(CXX) $(CFLAGS) -o lock_server $(LS) -lpthread; \
	fi

LT=$(LOCK_TESTER)
lock_tester : $(LT)
	if [ `uname` = "SunOS" ]; then \
		$(CXX) $(CFLAGS) -o lock_tester $(LT) -lpthread -lxnet; \
	else \
		$(CXX) $(CFLAGS) -o lock_tester $(LT) -lpthread; \
	fi

LD=$(LOCK_DEMO)
lock_demo: $(LD)
	if [ `uname` = "SunOS" ]; then \
		$(CXX) $(CFLAGS) -o lock_demo $(LD) -lpthread -lxnet; \
	else \
		$(CXX) $(CFLAGS) -o lock_demo $(LD) -lpthread; \
	fi

rpc.o: $(RPC)/rpc.cc
	$(CXX) -c $(RPC)/rpc.cc

chan.o: $(RPC)/chan.cc
	$(CXX) -c $(RPC)/chan.cc

host.o: $(RPC)/host.cc
	$(CXX) -c $(RPC)/host.cc

%.o : %.cc
	$(CXX) -c $(CFLAGS) $<

fuse.o: fuse.cc
	if [ `uname` = "linux" -o `uname` = "Linux" -o `uname` = "linux-gnu" ]; then \
                $(CXX) -c $(CFLAGS) $(FUSEFLAGS) $<; \
        else \
                $(CXX) -c $(CFLAGS) $(FUSEFLAGS) $(MACFLAGS) $<; \
        fi       

LAB1_SRC =  lock_server.cc lock_smain.cc lock_tester.cc lock_client.cc lock_protocol.h lock_client.h lock_server.h lock_demo.cc Makefile

LAB2_SRC =  yfs_protocol.h yfs_smain.cc yfs_server.cc yfs_server.h extent_protocol.h extent_client.cc extent_client.h extent_server.cc extent_server.h extent_smain.cc fuse.cc yfs_client.cc yfs_client.h start.sh stop.sh Makefile test-lab-2.pl

l1:
	./mklab.pl 1 0 l1 $(LAB1_SRC)
l2:
	./mklab.pl 2 0 l2 $(LAB2_SRC)

clean :
	rm -f *.o *.d extent_server yfs_server yfs_client lock_server lock_tester lock_demo

-include *.d
