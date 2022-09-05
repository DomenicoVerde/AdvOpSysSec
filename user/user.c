#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#define SET_HIGH_PRIORITY_FLOW 	_IO('D',1)
#define SET_LOW_PRIORITY_FLOW 	_IO('D',2)
#define SET_READ_ASYNC		_IO('D',3)
#define SET_READ_SYNC		_IO('D',4)
#define SET_WRITE_ASYNC		_IO('D',5)
#define SET_WRITE_SYNC		_IO('D',6)
#define SET_TIMEOUT		_IOW('D',6,long *)
#define MAX_SEGMENT_SIZE 	1024

int main(int argc, char *argv[]) {
/* Default User-Level Application to interact with MY_DEV */
	if (argc < 3) {
		printf("Usage: ./user.o device (write | read | ioctl) [data]\n");
		return -1;
	}
	
	// Open the Device for read and write
	int fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		printf("Error: Failed to open() device %s.\n", argv[1]);
		return -1;
	}
    
	// Do Selected Operation (I/O Ctl, Read or Write)
	if (strcmp(argv[2], "ioctl") == 0) {
		// Manage I/O Ctl Commands
		if (argc < 4) {
			printf("Error: insert the ioctl() command.\n");
			return -1;
		}
		
		if (strcmp(argv[3], "set_high_priority_flow") == 0)
   			return ioctl(fd, SET_HIGH_PRIORITY_FLOW);
		
		else if (strcmp(argv[3], "set_low_priority_flow") == 0)
   			return ioctl(fd, SET_LOW_PRIORITY_FLOW);
		
		else if (strcmp(argv[3], "set_read_async") == 0)
   			return ioctl(fd, SET_READ_ASYNC);
		
		else if (strcmp(argv[3], "set_read_sync") == 0)
   			return ioctl(fd, SET_READ_SYNC);
		
		else if (strcmp(argv[3], "set_write_async") == 0)
   			return ioctl(fd, SET_WRITE_ASYNC);
		
		else if (strcmp(argv[3], "set_write_sync") == 0)
   			return ioctl(fd, SET_WRITE_SYNC);
		
		else if (strcmp(argv[3], "set_timeout") == 0) {
			// Modify Timeout Value
			if (argc < 5) {
				printf("Error: insert the timeout value.\n");
				return -1;
			}
			
			long timeout = atol(argv[4]);
			
			if (timeout > 0)
   				return ioctl(fd, SET_TIMEOUT, (long *) &timeout);
   			else {
   				printf("Error: Invalid ioctl() timeout.\n");
   				return -1;
   			}
   			
		} else {
			printf("Error: invalid ioctl() command.\n");
			return -1;
		}
	
	} else if (strcmp(argv[2], "write") == 0) {
		// Manage Write Operation
		if (argc < 4) {
			printf("Error: insert the write() data.\n");
			return -1;
		}

		return write(fd, argv[3], strlen(argv[3]));
	
	} else if (strcmp(argv[2], "read") == 0) {
		// Manage Read Operation
		char data[MAX_SEGMENT_SIZE];
		int ret = read(fd, data, MAX_SEGMENT_SIZE);
		
		printf("%s\n", data);
		return ret;
	
	} else {
    		printf("Error: Invalid Operation Selected. Insert: write, read or ioctl.\n");
		return -1;
	}
	
	// Close the Device
	if (close(fd) != 0) {
		printf("Failed to close() device %s.\n", argv[1]);
		return -1;
	}
	
	return 0;
}
