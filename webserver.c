// webserver.c
// FrobozzCo Official Webserver
// Barbazzo Fernap barbazzo@gue.com
// Gustar Woomax gustar@gue.com
// Wilbar Memboob wilbar@gue.com

// By the Frobozz Magic Webserver Company
// Released under the Grue Public License
// Frobruary 14th, 1067 GUE

// THERE IS NO WARRANTY FOR THE PROGRAM, TO THE EXTENT PERMITTED
// BY APPLICABLE LAW. EXCEPT WHEN OTHERWISE STATED IN WRITING THE
// COPYRIGHT HOLDERS AND/OR OTHER PARTIES PROVIDE THE PROGRAM “AS IS”
// WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING,
// BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
// FITNESS FOR A PARTICULAR PURPOSE. THE ENTIRE RISK AS TO THE QUALITY
// AND PERFORMANCE OF THE PROGRAM IS WITH YOU. SHOULD THE PROGRAM PROVE
// DEFECTIVE, *AND IT WILL*, YOU ASSUME THE COST OF ALL NECESSARY
// SERVICING, REPAIR OR CORRECTION.

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>

#define _XOPEN_SOURCE

typedef struct {
	char *method;
	char *uri;
	char *version;
	char *headers;
} httpreq_t;

void *data_thread(void *sockfd_ptr);


/* NOTE: this function is based on a function provided in the GNU "timegm" man
   page. timegm is a GNU extension to time.h that returns the given tm struct as
   a UNIX timestamp in GMT/UTC, rather than local time. The man page suggests a
   function similar to the one below as a portable equivalent.
 */
time_t my_timegm(struct tm *tm) {
	time_t ret;
	char *tz;

	tz = getenv("TZ");
	putenv("TZ=GMT");
	tzset();
	ret = mktime(tm);
	if (tz) {
		char envstr[strlen(tz) + 4];
		envstr[0] = '\0';
		strcat(envstr, "TZ=");
		strcat(envstr, tz);
		putenv(envstr);
	} else {
		putenv("TZ=");
	}

	tzset();

	return ret;
}

char *get_header(const httpreq_t *req, const char* headername) {
	char *hdrptr;
	char *hdrend;
	char *retval = NULL;

	char searchstr[strlen(headername) + 5];
	strcpy(searchstr, "\r\n");
	strcat(searchstr, headername);
	strcat(searchstr, ": ");

	if (hdrptr = strstr(req->headers, searchstr)) {
		hdrptr += strlen(searchstr);
		if (hdrend = strstr(hdrptr, "\r\n")) {
			char hdrval[1024]; // temporary return value
			memcpy((char *)hdrval, hdrptr, (hdrend - hdrptr));
			hdrval[hdrend - hdrptr] = '\0'; // tack null onto end of header value
			int hdrvallen = strlen(hdrval);
			retval = (char *)malloc((hdrvallen + 1) * sizeof(char)); // malloc a space for retval
			strcpy(retval, (char *)hdrval);
		} else {
			retval = (char *)malloc((strlen(hdrptr) + 1) * sizeof(char)); //
			strcpy(retval, hdrptr);
		}
	}

	return retval;
}

/* As long as str begins with a proper HTTP-Version followed by delim, returns a
   pointer to the start of the version number (e.g., 1.0). Returns NULL otherwise.
 */
char *http_version_str(char *str, char *delim) {
	char *vstart = strstr(str, "HTTP/");
	char *vnumstart = str + 5;
	char *vdot = strchr(str, '.');
	char *vend = strstr(str, delim);
	char *digits = "0123456789";
	int majvlen = 0;
	int minvlen = 0;

	if (!vstart || !vdot // something's missing
		|| vstart != str) // str doesn't start with "HTTP/"
		return NULL;

	majvlen = strspn(vnumstart, digits);
	minvlen = strspn(vdot + 1, digits);

	if (majvlen < 1 || (vnumstart + majvlen) != vdot // bad major version
		|| minvlen < 1 || (vdot + minvlen + 1) != vend) // bad minor version
		return NULL;

	return vnumstart;
}

/* Fills req with the request data from datastr. Returns 0 on success.
 */

int parsereq(httpreq_t *req, char *datastr) {
	char *position;
	char *last_position = datastr;
	char *temp_position;
	int matchlen;

	req->method = "";
	req->uri = "";
	req->version = "";
	req->headers = "";

	if (!(position = strchr(last_position, ' '))) {
		return 1;
	}
	matchlen = (int)(position - last_position);
	req->method = (char *)malloc((matchlen + 1) * sizeof(char));
	memcpy(req->method, last_position, matchlen);
	req->method[matchlen] = '\0';
	last_position = position + 1;

	if (!(position = strchr(last_position, ' '))
		&& !(position = strstr(last_position, "\r\n"))) {
		return 1;
	}

	// strip any query string out of the URI
	if ((temp_position = strchr(last_position, '?')) && temp_position < position)
		matchlen = (int)(temp_position - last_position);
	else
		matchlen = (int)(position - last_position);

	req->uri = (char *)malloc((matchlen + 1) * sizeof(char));
	memcpy(req->uri, last_position, matchlen);
	req->uri[matchlen] = '\0';
	if (position[0] == '\r') {
		req->version = "0.9";
		req->headers = "";
		return 0; // simple req -- uri only
	}

	// If we get here, it's a full request, get the HTTP version and headers
	last_position = position + 1;

	if (!(position = strstr(last_position, "\r\n"))
		|| !(last_position = http_version_str(last_position, "\r\n"))) {
		return 1;
	}

	matchlen = (int)(position - last_position);
	req->version = (char *)malloc((matchlen + 1) * sizeof(char));
	memcpy(req->version, last_position, matchlen);
	req->version[matchlen] = '\0';
	last_position = position;

	req->headers = (char *)malloc(strlen(last_position) * sizeof(char));
	strcpy(req->headers, last_position);

	return 0;
}

char *contype(char *ext) {
	if (strcmp(ext, "html") == 0) return "text/html";
	else if (strcmp(ext, "htm") == 0) return "text/html";
	else if (strcmp(ext, "jpeg") == 0) return "image/jpeg";
	else if (strcmp(ext, "jpg") == 0) return "image/jpeg";
	else if (strcmp(ext, "gif") == 0) return "image/gif";
	else if (strcmp(ext, "txt") == 0) return "text/plain";
	else return "application/octet-stream";

}

char *status(int statcode) {
	if (statcode == 200) 	return "200 OK";
	else if (statcode == 304) return "304 Not Modified";
	else if (statcode == 400) return "400 Bad Request";
	else if (statcode == 403) return "403 Forbidden";
	else if (statcode == 404) return "404 Not Found";
	else if (statcode == 500) return "500 Internal Server Error";
	else if (statcode == 501) return "501 Not Implemented";
	else return "";
}

int send_response(int sockfd, httpreq_t *req, int statcode) {
    int urifd = -1; //  Initialize urifd
    const int BUFSIZE = 1024;
    char sendmessage[BUFSIZE];
    char *path = NULL; //  Initialize path
    char path_buffer[BUFSIZE]; //  Local buffer for path manipulation
    struct stat stbuf = {0}; //  Initialize stat buffer

    //  Initial NULL checks for request struct members
    if (req == NULL || req->uri == NULL || req->method == NULL ||
        req->headers == NULL || req->version == NULL) {
        return 0;
    }

    //  Make a mutable copy of the URI for path processing
    strncpy(path_buffer, req->uri, sizeof(path_buffer) - 1);
    path_buffer[sizeof(path_buffer) - 1] = '\0';
    path = path_buffer;


    //  More robust path validation and normalization logic
    if ((path[0] == '/') || ((strstr(path, "http://") == path)
                             && (path = strchr(path + 7,  '/')))) {
        if (path != NULL) {
             path += 1;
             if (path[0] == '\0') {
                 //  Check space before copying "index.html"
                 if (strlen(path_buffer) + strlen("index.html") < sizeof(path_buffer)) {
                    strcpy(path, "index.html");
                 } else {
                     statcode = 400;
                     path = NULL;
                 }
             } else if (path[strlen(path) - 1] == '/') {
                 //  Check space before appending "index.html"
                 if (strlen(path) + strlen("index.html") < sizeof(path_buffer)) {
                    strcat(path, "index.html"); //  Use strcat only after size check
                 } else {
                     statcode = 400;
                     path = NULL;
                 }
             }
        } else {
             statcode = 400;
             path = NULL;
        }
    } else {
        statcode = 400;
        path = NULL;
    }

    //  Handle ".." path traversal attempt more explicitly with 403
    if (path != NULL && strstr(path, "..") != NULL) {
        statcode = 403; // Forbidden
        path = NULL;
    }

    //  Consolidated file access check and stat logic
    if (statcode == 200 && path != NULL) {
        urifd = open(path, O_RDONLY, 0);
        if (urifd < 0) {
            if (errno == ENOENT || errno == ENOTDIR) {
                statcode = 404;
            } else if (errno == EACCES) {
                statcode = 403;
            } else {
                perror("open");
                statcode = 500;
            }
        } else {
             //  Use fstat on the open file descriptor
             if (fstat(urifd, &stbuf) == -1) {
                 perror("fstat");
                 statcode = 500;
                 close(urifd);
                 urifd = -1;
             }
        }
    } else if (statcode == 200 && path == NULL) {
         if (statcode == 200) statcode = 400;
    }


    sendmessage[0] = '\0';
    int current_len = 0; //  Track current length in buffer
    int remaining_space = sizeof(sendmessage) - 1; //  Track remaining space
    int n; //  Variable for snprintf return value

    if (strcmp(req->version, "0.9") != 0) {
        char *ext = "";
        time_t curtime;
        char timebuf[30]; //  Buffer for formatted time

        if ((statcode == 200 || statcode == 304) && path != NULL) { //  Determine extension more safely
            char *dot = strrchr(path, '.');
            if (dot) ext = dot + 1;
        } else {
             ext = "html";
        }

        //  Conditional GET logic refined
        if (statcode == 200 && urifd >= 0 && strcmp(req->method, "GET") == 0) {
            char *imstime_str = get_header(req, "If-Modified-Since");
            if (imstime_str) {
                struct tm imstime_tm = {0};
                if (strptime(imstime_str, "%a, %d %b %Y %H:%M:%S GMT", &imstime_tm) ||
                    strptime(imstime_str, "%a, %d-%b-%y %H:%M:%S GMT", &imstime_tm) ||
                    strptime(imstime_str, "%a %b %d %H:%M:%S %Y", &imstime_tm)) {
                    if (stbuf.st_mtime <= my_timegm(&imstime_tm)) {
                        statcode = 304;
                    }
                }
                free(imstime_str); //  Free memory from get_header
            }
        }

        time(&curtime);
        strftime(timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&curtime)); //  Use strftime for Date header

        //  Use snprintf instead of strcat for all header lines
        n = snprintf(sendmessage + current_len, remaining_space, "HTTP/1.0 %s\r\n", status(statcode));
        if (n < 0 || n >= remaining_space) goto buffer_full; //  Check for buffer overflow/error
        current_len += n; remaining_space -= n;

        n = snprintf(sendmessage + current_len, remaining_space, "Date: %s\r\n", timebuf);
        if (n < 0 || n >= remaining_space) goto buffer_full; //  Check for buffer overflow/error
        current_len += n; remaining_space -= n;

        n = snprintf(sendmessage + current_len, remaining_space, "Server: Frobozz Magic Software Company Webserver v.002\r\n");
        if (n < 0 || n >= remaining_space) goto buffer_full; //  Check for buffer overflow/error
        current_len += n; remaining_space -= n;

        //  Last-Modified header
        if ((statcode == 200 || statcode == 304) && urifd >= 0) {
             strftime(timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&stbuf.st_mtime));
             n = snprintf(sendmessage + current_len, remaining_space, "Last-Modified: %s\r\n", timebuf);
             if (n < 0 || n >= remaining_space) goto buffer_full;
             current_len += n; remaining_space -= n;
        }

        //  Content-Type logic adjusted
        if (statcode != 304) {
             n = snprintf(sendmessage + current_len, remaining_space, "Content-Type: %s\r\n", contype(ext));
             if (n < 0 || n >= remaining_space) goto buffer_full; //  Check for buffer overflow/error
             current_len += n; remaining_space -= n;
        }

        //  Content-Length header
        if (statcode == 200 && strcmp(req->method, "HEAD") != 0 && urifd >= 0) {
              n = snprintf(sendmessage + current_len, remaining_space, "Content-Length: %ld\r\n", (long)stbuf.st_size);
              if (n < 0 || n >= remaining_space) goto buffer_full; //  Check for buffer overflow/error
              current_len += n; remaining_space -= n;
        }

        n = snprintf(sendmessage + current_len, remaining_space, "Connection: close\r\n\r\n");
        if (n < 0 || n >= remaining_space) goto buffer_full; //  Check for buffer overflow/error
        current_len += n; remaining_space -= n;

    }

    if (statcode != 200 && statcode != 304) { //  Build error body safely using snprintf
        const char* stat_msg = status(statcode);
        const char* uri_display = (req->uri != NULL) ? req->uri : "[unknown]";

        n = snprintf(sendmessage + current_len, remaining_space,
                     "<html><head><title>%s</title></head>"
                     "<body><h2>HTTP/1.0</h2><h1>%s</h1>"
                     "<h2>URI: %s</h2></body></html>",
                     stat_msg, stat_msg, uri_display);

        if (n < 0 || n >= remaining_space) {
              sendmessage[sizeof(sendmessage) - 1] = '\0'; //  Ensure null termination on overflow
              current_len = sizeof(sendmessage) - 1;
        } else {
             current_len += n; remaining_space -= n;
        }
    }

    if (current_len > 0) { //  Send based on calculated current_len
        if (send(sockfd, sendmessage, current_len, 0) < 0) {
            perror("send header");
            goto cleanup_and_exit; //  Use goto for cleanup
        }
    }

    if (statcode == 200 && urifd >= 0 && (strcmp(req->method, "HEAD") != 0)) {
        int readbytes;
        char filebuf[BUFSIZE]; //  Use separate buffer for file reading

        while ((readbytes = read(urifd, filebuf, sizeof(filebuf))) > 0) {
            if (send(sockfd, filebuf, readbytes, 0) < 0) {
                perror("send body");
                goto cleanup_and_exit; //  Use goto for cleanup
            }
        }
        if (readbytes < 0) {
            perror("read file");
        }
    }

    goto cleanup_and_exit; //  Normal exit path leads to cleanup

buffer_full: //  Label for buffer overflow during header creation
    fprintf(stderr, "Error: Response header exceeded buffer size (%d bytes).\n", BUFSIZE);
    const char *errMsg = "HTTP/1.0 500 Internal Server Error\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
    send(sockfd, errMsg, strlen(errMsg), 0);

cleanup_and_exit: //  Label for cleanup code
    if (urifd >= 0) {
        close(urifd); //  Ensure file descriptor is closed
    }

    //  Free memory allocated by parsereq (assuming it allocates these)
    if (req != NULL) {
         if (req->method && req->method[0] != '\0') free(req->method);
         if (req->uri && req->uri[0] != '\0') free(req->uri);
         if (req->version && req->version[0] != '\0') free(req->version);
         if (req->headers && req->headers[0] != '\0') free(req->headers);
    }

    return 0; //  Return 0 consistent with original void* expectation in data_thread context (though function returns int)
}

int main(int argc, char *argv[]) {
	int acc, sockfd, clen, port;
	struct hostent *he;
	struct sockaddr_in caddr, saddr;

	if(argc <= 1) {

		fprintf(stderr, "No port specified. Exiting!\n");
		exit(1);

	}

	port = atoi(argv[1]);

	/* Obtain name and address for the local host */
	if((he=gethostbyname("localhost"))==NULL) {

		herror("gethostbyname");
		exit(1);

	}

	/* Open a TCP (Internet Stream) socket */
	if((sockfd=socket(AF_INET,SOCK_STREAM,0)) == -1) {

		perror("socket");
		exit(1);

	}

	/* Create socket address structure for the local host */
	memset((char *) &saddr, '\0', sizeof(saddr));
	saddr.sin_family=AF_INET;
	saddr.sin_port=htons(port);
	saddr.sin_addr.s_addr=htonl(INADDR_ANY);

	/* Bind our local address so that the client can send to us */
	if(bind(sockfd,(struct sockaddr *) &saddr,sizeof(saddr)) == -1) {
		perror("bind");
		exit(1);
	}

	if(listen(sockfd,5) < 0) {
		perror("listen");
		exit(1);
	}

	/* Infinite loop for receiving and processing client requests */
	for(;;) {
		clen=sizeof(caddr);

		/* Wait for a connection for a client process */
		acc=accept(sockfd,(struct sockaddr *) &caddr,(socklen_t*)&clen);
		if(acc < 0) {
			perror("accept");
			exit(1);
		} else {
			pthread_t *thread = (pthread_t *) malloc(sizeof(pthread_t));
			int *sockfd_ptr = (int *) malloc(sizeof(int));

			*sockfd_ptr = acc;
			pthread_create(thread, NULL, data_thread, sockfd_ptr);
		}
	}

	return 0;
}
