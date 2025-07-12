#!/bin/bash

for i in {1..10}; do
  (
    echo "user$i"
    sleep 1
    echo "/sendfile test$i.txt Tester"
    sleep 2
    echo "/exit"
  ) | ./chatclient 127.0.0.1 9000 &
done
