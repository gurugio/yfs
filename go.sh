#!/bin/bash
export RPC_LOSSY=0
for i in {0..9}; do
    ./lock_server 33333 &
    SERVER_PID=$!
    ./lock_tester 33333
    kill -9 $!
    echo "FINISH TEST #"$i
    sleep 1
done
