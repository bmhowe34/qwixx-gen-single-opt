#!/bin/bash

while sleep 1; do 
  iterCount=`cat mylog | grep Finish | wc -l`
  iterP1=$((iterCount+1))
  echo -n "Start  " >> mylog
  date>>mylog
  echo "Executing./a.out ${iterP1} 8"
  ./a.out ${iterP1} 8
  echo -n "Finish " >>mylog
  date>>mylog
  sleep 1
  #od -t f4 iter_results.bin | head > iter${iterCount}.txt
done
