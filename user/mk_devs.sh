#!/usr/bin/bash

for i in {0..127}; do
    	sudo mknod -m 777 /dev/my_dev$i c 511 $i
done
