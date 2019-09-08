// cc -o rd rd.c -Wall -Wextra -std=c99 -pedantic -lm -lcurl

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <curl/curl.h>
#include "hec_key.h"

typedef struct {
        long long pulse_number;
        long long interrupt_time;
        long long interrupt_delta;
} e_fifo;

#define SIZE 100
#define BUFFERSIZE	(SIZE*sizeof(e_fifo))
#define OUTBUFFER	(80*SIZE)	// Look at size of a printed JSON element

size_t write_callback(__attribute__((unused)) void *buffer,
		__attribute__((unused)) size_t size,
		size_t nmemb,
		__attribute__((unused)) void *userp)
{
	return size * nmemb;
}

int main()
{
	char buffer[BUFFERSIZE];
	char msg[OUTBUFFER];
	int f, rbytes, elements;
	e_fifo *element_ptr;
        CURL *curl;

	element_ptr = (e_fifo *)buffer;

	//printf("%d\n", sizeof(e_fifo));

	if ((f = open("dev0", O_RDONLY)) < 0) {
		fprintf(stderr, "Error open\n");
		return 1;
	}
	
	while (1) {
		int msgptr = 0;

		rbytes = read(f, buffer, BUFFERSIZE);
		elements = rbytes / sizeof(e_fifo);

		//printf("Bytes read: %d elements read: %d\n", rbytes, elements);

		msgptr += sprintf(&msg[msgptr], "{\"event\":[");
		for (int i=0; i<elements; i++) {
			msgptr += sprintf(&msg[msgptr], "{\"pulseNumber\": %lld, \"time\": %lld, \"delta\": %lld},", (element_ptr+i)->pulse_number, (element_ptr+i)->interrupt_time, (element_ptr+i)->interrupt_delta);
		}
		msgptr += sprintf(&msg[--msgptr], "]}");

		//fprintf(stdout, "%s\n", msg);

		curl = curl_easy_init();
		if (curl) {
			CURLcode res;
			struct curl_slist *hdr = NULL;

			hdr = curl_slist_append(hdr, SPLUNK_HEC_KEY);
			curl_easy_setopt(curl, CURLOPT_URL, "https://192.168.5.201:8443/services/collector");
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, msg);
			curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, msgptr);
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
			//curl_easy_setopt(curl, CURLOPT_POST, 1L);
			//curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
			//curl_easy_setopt(curl, CURLOPT_READDATA, &wt);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
			//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
			res = curl_easy_perform(curl);
			if(res != CURLE_OK) fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
			// always cleanup
			curl_easy_cleanup(curl);
			// free the custom headers
			curl_slist_free_all(hdr);
		}
	}

	return 0;
}
