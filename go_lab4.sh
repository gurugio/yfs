#!/bin/bash
./start.sh 5
./test-lab-3-a.pl ./yfs1
./stop.sh
./stop.sh
./stop.sh

./start.sh 5
./test-lab-3-b ./yfs1 ./yfs2
./stop.sh
./stop.sh
./stop.sh

./start.sh 5
./test-lab-3-c ./yfs1 ./yfs2
./stop.sh
./stop.sh
./stop.sh
