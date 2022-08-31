#define _GNU_SOURCE

#include <fcntl.h>
#include<stdio.h>
#include<stdlib.h>
#include<pthread.h>
#include<unistd.h>
#include<errno.h>
#include<string.h>

#define THREADS 600
#define DEVICES 128
#define PROBABILITY 50

void *reader_function(void *tid) {
/* Read Function for a Thread 
 */
 	char *device;
	long n = ((long) tid) % DEVICES;
	asprintf(&device,"/dev/my_dev%ld", n);
	
	// Open the Device
	int fd = open(device,  O_RDWR);
	if (fd < 0) {
		printf ("Error opening device %s: %s\n", device, strerror(errno));
		pthread_exit(NULL);
	}
	
	// Read a Segment from it
	char data[16] = "";
	int ret = read(fd, data, 16);
	if (ret < 0) {
		printf ("Error reading from device %s: %s\n", device, strerror(errno));
		pthread_exit(NULL);
	}
	
	// Close the Device
	if (close(fd) != 0) {
		printf("Error closing device %s: %s\n", device, strerror(errno));
		pthread_exit(NULL);
	}
}


void *writer_function(void *tid) {
/* Write Function for a Thread 
 */
	char *device;
	long n = ((long) tid) % DEVICES;
	asprintf(&device,"/dev/my_dev%ld", n);
	
	// Open the Device
	int fd = open(device,  O_RDWR);
	if (fd < 0) {
		printf ("Error opening device %s: %s\n", device, strerror(errno));
		pthread_exit(NULL);
	}
	
	// Write a Segment on it
	char *data;
	asprintf(&data,"thread %ld\n", (long) tid);
	int ret = write(fd, data, 16);
	if (ret < 0) {
		printf ("Error writing to device %s: %s\n", device, strerror(errno));
		pthread_exit(NULL);
	}
	
	// Close the Device
	if (close(fd) != 0) {
		printf("Error closing device %s: %s\n", device, strerror(errno));
		pthread_exit(NULL);
	}
}



int main(int argc, char *argv[]) {
/* This program should be used to test concurrency the module "MY_DEV". */

	long i, ret;
	pthread_t tid[THREADS];
	
	for (i = 0; i < THREADS; i++) {
		// Creates random writers and readers
		int p = rand() % 100;
		if (p < PROBABILITY) 
			ret = pthread_create(&tid[i], NULL, reader_function, (void *) i);
		else 
			ret = pthread_create(&tid[i], NULL, writer_function, (void *) i);
			
		if (ret != 0) {
			printf ("Error creating Thread %ld.\n", i);
			exit(EXIT_FAILURE);
	    	}
	}
	
	// Wait until threads terminate
	for (i = 0; i < THREADS; i++) {
		if (pthread_join(tid[i], NULL)) {
			printf ("Error joining Thread %ld.\n", i);
			exit(EXIT_FAILURE);
	    	}	
	}
	
	exit(EXIT_SUCCESS);
}
