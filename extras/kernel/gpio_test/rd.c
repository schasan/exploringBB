#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct {
        long long pulse_number;
        long long interrupt_time;
        long long interrupt_delta;
} e_fifo;

#define SIZE 100
#define BUFFERSIZE	(SIZE*sizeof(e_fifo))

int main()
{
	char buffer[BUFFERSIZE];
	int f, rbytes, elements;
	e_fifo *element_ptr;

	element_ptr = (e_fifo *)buffer;

	//printf("%d\n", sizeof(e_fifo));

	if ((f = open("dev0", O_RDONLY)) < 0) {
		fprintf(stderr, "Error open\n");
		return 1;
	}
	
	rbytes = read(f, buffer, BUFFERSIZE);
	elements = rbytes / sizeof(e_fifo);

	printf("Bytes read: %d elements read: %d\n", rbytes, elements);

	for (int i=0; i<elements; i++) {
		printf("%lld %lld %lld\n", (element_ptr+i)->pulse_number, (element_ptr+i)->interrupt_time, (element_ptr+i)->interrupt_delta);
	}

	return 0;
}
