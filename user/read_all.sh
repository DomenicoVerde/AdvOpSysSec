#!/usr/bin/bash

for i in {0..127}; do
    	cat /dev/my_dev$i
done
