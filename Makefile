obj-m += my_dev.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules 

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f *.ko
	rm -f *.mod
	rm -f .*.cmd
	rm -f *.o
	rm -f *.order
	rm -f *.mod.c
	rm -f *.order
	rm -f *.symvers	
