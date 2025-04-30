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

#define _XOPEN_SOURCE // Required for strptime

typedef struct {
	char *method;
	char *uri;
	char *version;
	char *headers;
} httpreq_t;


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

	if ((hdrptr = strstr(req->headers, searchstr))) {
		hdrptr += strlen(searchstr);
		if ((hdrend = strstr(hdrptr, "\r\n"))) {
			char hdrval[1024]; // temporary return value
			memcpy((char *)hdrval, hdrptr, (hdrend - hdrptr));
			hdrval[hdrend - hdrptr] = '\0'; // tack null onto end of header value
			int hdrvallen = strlen(hdrval);
			retval = (char *)malloc((hdrvallen + 1) * sizeof(char)); // malloc a space for retval
            if(retval) { // Check malloc result
			    strcpy(retval, (char *)hdrval);
            }
		} else {
            // Header value extends to the end
            size_t val_len = strlen(hdrptr);
			retval = (char *)malloc(val_len + 1); //
            if(retval) { // Check malloc result
			    strcpy(retval, hdrptr);
            }
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
    if (!req->method) return 1; // Check malloc
	memcpy(req->method, last_position, matchlen);
	req->method[matchlen] = '\0';
	last_position = position + 1;

	if (!(position = strchr(last_position, ' '))
		&& !(position = strstr(last_position, "\r\n"))) {
        free(req->method); req->method = NULL; // Cleanup
		return 1;
	}

	// strip any query string out of the URI
	if ((temp_position = strchr(last_position, '?')) && temp_position < position)
		matchlen = (int)(temp_position - last_position);
	else
		matchlen = (int)(position - last_position);

	req->uri = (char *)malloc((matchlen + 1) * sizeof(char));
    if (!req->uri) { free(req->method); req->method = NULL; return 1; } // Check malloc & cleanup
	memcpy(req->uri, last_position, matchlen);
	req->uri[matchlen] = '\0';
	if (position[0] == '\r') {
		req->version = "0.9"; // Assign string literal (original behavior)
		req->headers = ""; // Assign string literal (original behavior)
		return 0; // simple req -- uri only
	}

	// If we get here, it's a full request, get the HTTP version and headers
	last_position = position + 1;

    char *version_start;
	if (!(position = strstr(last_position, "\r\n"))
		|| !(version_start = http_version_str(last_position, "\r\n"))) { // Use temp var
        free(req->method); req->method = NULL; // Cleanup
        free(req->uri); req->uri = NULL; // Cleanup
		return 1;
	}

	matchlen = (int)(position - version_start); // Use version_start
	req->version = (char *)malloc((matchlen + 1) * sizeof(char));
    if (!req->version) { free(req->method); free(req->uri); return 1; } // Check malloc & cleanup
	memcpy(req->version, version_start, matchlen); // Use version_start
	req->version[matchlen] = '\0';
	last_position = position;

	req->headers = (char *)malloc(strlen(last_position) + 1); // Add 1 for null
    if (!req->headers) { free(req->method); free(req->uri); free(req->version); return 1; } // Check malloc & cleanup
	strcpy(req->headers, last_position);

	return 0;
}

char *contype(char *ext) {
    if (!ext) return "application/octet-stream"; // Handle NULL ext
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


// --- Start of Modified Section ---

int send_response(int sockfd, httpreq_t *req, int statcode) {
	int urifd = -1; // Changed: Initialize to -1 to indicate not open
	const int BUFSIZE = 1024;
	char sendmessage[BUFSIZE];
    // char *path = req->uri; // Changed: Don't modify req->uri directly, use a copy
    char path_buffer[BUFSIZE]; // Added: Local buffer for path manipulation
    char *path = NULL; // Added: Pointer to the path being used (either original or modified)
    char *imstime = NULL; // Added: Define imstime here for cleanup scope
    struct stat stbuf = {0}; // Added: Define stbuf here for broader scope if needed
    size_t current_len = 0; // Added: Track current length of sendmessage
    int ok = 1; // Added: Flag to track if buffer operations succeeded

	// Added: Basic NULL check for request struct members
    if (req == NULL || req->uri == NULL || req->method == NULL ||
		req->headers == NULL || req->version == NULL) {
		// Cannot proceed without valid request parts. Original code just returned 0.
		return 0;
	}

    // Added: Copy URI to local buffer for safe modification
    // Use strncpy for safety against overly long URIs in req->uri
    strncpy(path_buffer, req->uri, sizeof(path_buffer) - 1);
    path_buffer[sizeof(path_buffer) - 1] = '\0'; // Ensure null termination
    path = path_buffer; // Work with the copy


	if ((path[0] == '/') || ((strstr(path, "http://") == path)
							 && (path = strchr(path + 7,  '/')))) {
		// Added: Check result of strchr
        if (path) {
		    path += 1; // remove leading slash (or slash after hostname)
		    if (path[0] == '\0') {  // substituting in index.html for a blank URL!
			    // path = "index.html"; // Changed: Copy safely instead of pointer assignment
                // Check if "index.html" fits in path_buffer
                if (strlen("index.html") < sizeof(path_buffer)) {
                    strcpy(path_buffer, "index.html");
                    path = path_buffer; // Point path to the start of the buffer again
                } else {
                    statcode = 400; // Indicate error - path too long potentially
                    path = NULL; // Mark path as invalid
                }
		    } else if (path[strlen(path) - 1] == '/') {
			    //concatenating index.html for a /-terminated URL!
			    // strcat(path, "index.html"); // Changed: Unsafe strcat, use safe append
                // Check if appending "index.html" fits
                size_t current_path_len = strlen(path);
                // Check space needed: current len + len("index.html") + null terminator
                if (current_path_len + strlen("index.html") + 1 <= sizeof(path_buffer)) {
                    strcat(path, "index.html"); // Safe now due to check
                } else {
                    statcode = 400; // Indicate error - path too long
                    path = NULL; // Mark path as invalid
                }
		    }
        } else {
            // http:// URI without a path part
            statcode = 400;
            path = NULL;
        }
	} else {
		statcode = 400;
        path = NULL; // Added: Mark path as invalid
	}

    // Changed: Check path for NULL before using strstr
	// if (strstr(path, "..") != NULL) {
    // Changed: Handle ".." path traversal attempt more explicitly with 403
    if (path != NULL && strstr(path, "..") != NULL) {
		// statcode = 500; // Original code used 500, 403 is more appropriate
        statcode = 403; // Forbidden
        path = NULL; // Added: Mark path as invalid
	}

    // Changed: Check path for NULL before trying to open
	// if (statcode == 200 && (urifd = open(path, O_RDONLY, 0)) < 0) {
    if (statcode == 200 && path != NULL) {
        urifd = open(path, O_RDONLY, 0);
        if (urifd < 0) {
		    if (errno == ENOENT || errno == ENOTDIR) { // file or directory doesn't exist
			    statcode = 404;
		    } else if (errno == EACCES) { // access denied
			    statcode = 403;
		    } else {
			    // some other file access problem
                perror("open"); // Added: Print specific error
			    statcode = 500;
		    }
        } else {
             // Added: Get file stats using fstat if open succeeded
             if (fstat(urifd, &stbuf) == -1) {
                 perror("fstat");
                 statcode = 500;
                 close(urifd); // Close fd on error
                 urifd = -1;
             }
        }
	} else if (statcode == 200 && path == NULL) {
        // Added: If path became NULL due to earlier error, update statcode
        if (statcode == 200) statcode = 400; // Default to Bad Request
    }


	sendmessage[0] = '\0'; // Start with an empty string
    current_len = 0; // Reset length tracker

	if (strcmp(req->version, "0.9") != 0) { // full request
		char *ext = NULL; // Changed: Initialize to NULL
		time_t curtime;
		// char *imstime = NULL; // Defined outside block now
		struct tm tm = {0}; // Changed: Initialize struct tm
		// struct stat stbuf = {0}; // Defined outside block now
        char timebuf[30]; // Added: Buffer for formatted time string
        char len_buf[32]; // Added: Buffer for content length string

		// Changed: Determine extension only if path is valid and file might be accessible
        if ((statcode == 200 || statcode == 304) && path != NULL) {
            char *dot = strrchr(path, '.');
            if (dot) ext = dot + 1; // Point after the '.'
			else ext = ""; // No extension found
		} else {
			// errors are always html messages
			ext = "html";
		}

		// Conditional GET
        // Use statcode < 400 to allow 304 checks too, ensure file was opened (urifd >= 0)
        if (statcode < 400 && urifd >= 0 && strcmp(req->method, "GET") == 0) {
             // stbuf should already be populated from the fstat call after open
             if (stbuf.st_mtime != 0) { // Check if fstat succeeded earlier
                 imstime = get_header(req, "If-Modified-Since"); // Allocate memory
                 if (imstime) {
			        if (!strptime(imstime, "%a, %d %b %Y %H:%M:%S GMT", &tm)
				        && !strptime(imstime, "%a, %d-%b-%y %H:%M:%S GMT", &tm)
				        && !strptime(imstime, "%a %b %d %H:%M:%S %Y", &tm)) {
				        // badly formatted date - ignore header per RFC
			        } else {
                        // Parsed successfully, compare time
			            if (stbuf.st_mtime <= my_timegm(&tm)) {
				            // Not Modified
				            statcode = 304;
			            }
                    }
                    // Don't free imstime here, free at the end of the function
                 }
             } else if (statcode == 200) {
                 // Open succeeded but fstat failed earlier. statcode should already be 500.
             }
        }

		time(&curtime); // time for Date: header

        // --- Start Safe String Construction ---
        // Added: Check remaining space before each append operation
        #define CHECK_SPACE(needed) (current_len + (needed) < BUFSIZE)

        // Added: Append string safely, update current_len, set ok flag on failure
        #define SAFE_APPEND(str) \
            do { \
                if (ok) { \
                    size_t len_to_add = strlen(str); \
                    if (!CHECK_SPACE(len_to_add + 1)) { /* +1 for null terminator */ \
                        fprintf(stderr, "Error: Buffer overflow prevented for '%s'.\n", str); \
                        ok = 0; \
                    } else { \
                        memcpy(sendmessage + current_len, str, len_to_add + 1); /* Copy null too */ \
                        current_len += len_to_add; \
                    } \
                } \
            } while(0)

        // Added: Append N characters safely, update current_len, set ok flag on failure
        #define SAFE_APPEND_N(str, n) \
            do { \
                if (ok) { \
                    size_t len_to_add = strnlen(str, n); /* Calculate actual length up to n */ \
                    if (!CHECK_SPACE(len_to_add + 1)) { /* +1 for null terminator */ \
                        fprintf(stderr, "Error: Buffer overflow prevented for '%.*s'.\n", (int)n, str); \
                        ok = 0; \
                    } else { \
                        memcpy(sendmessage + current_len, str, len_to_add); \
                        current_len += len_to_add; \
                        sendmessage[current_len] = '\0'; /* Ensure null termination */ \
                    } \
                } \
            } while(0)

        // Changed: Use SAFE_APPEND and SAFE_APPEND_N instead of strcat/strncat
        SAFE_APPEND("HTTP/1.0 ");
        SAFE_APPEND(status(statcode));
        SAFE_APPEND("\r\nDate: ");
        // SAFE_APPEND_N(asctime(gmtime(&curtime)), 24); // Changed: Use strftime for safety and correctness
        strftime(timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&curtime));
        SAFE_APPEND(timebuf);
        SAFE_APPEND("\r\nServer: Frobozz Magic Software Company Webserver v.002");
        SAFE_APPEND("\r\nConnection: close");

        // Added: Add Content-Length and Last-Modified headers where appropriate
        if (ok && statcode < 400 && urifd >= 0 && stbuf.st_mtime != 0) {
            strftime(timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&stbuf.st_mtime));
            SAFE_APPEND("\r\nLast-Modified: ");
            SAFE_APPEND(timebuf);

            if (statcode == 200 && strcmp(req->method, "HEAD") != 0) {
                sprintf(len_buf, "%ld", (long)stbuf.st_size);
                SAFE_APPEND("\r\nContent-Length: ");
                SAFE_APPEND(len_buf);
            }
        }

        // Changed: Add Content-Type header safely
        if (ok && statcode != 304) { // Don't send Content-Type for 304
            SAFE_APPEND("\r\nContent-Type: ");
            SAFE_APPEND(contype(ext));
        }
        // Changed: Add final CRLF safely
        SAFE_APPEND("\r\n\r\n");

	} // End if (strcmp(req->version, "0.9") != 0)

    // Changed: Build error message body safely if needed and buffer had space
	// if (statcode != 200) {
    if (ok && statcode >= 400) { // Changed: Check ok flag and if statcode is an error
        const char* stat_msg = status(statcode);
        // Added: Use original URI for display in error message
        const char* uri_display = (req->uri != NULL) ? req->uri : "[unknown]";

        // Changed: Use SAFE_APPEND for error message parts
		SAFE_APPEND("<html><head><title>");
		SAFE_APPEND(stat_msg);
		SAFE_APPEND("</title></head><body><h2>HTTP/1.0</h2><h1>");
		SAFE_APPEND(stat_msg);
		SAFE_APPEND("</h1><h2>URI: ");
		SAFE_APPEND(uri_display); // Changed: Use original URI
		SAFE_APPEND("</h2></body></html>");
	}

    // Added: Check 'ok' flag before sending. If not ok, buffer overflow occurred.
    if (!ok) {
        // Buffer overflow happened during message construction. Send minimal error.
        fprintf(stderr, "Internal Server Error: Response construction exceeded buffer size.\n");
        const char *errMsg = "HTTP/1.0 500 Internal Server Error\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        // Ignore potential errors sending the error message itself
        send(sockfd, errMsg, strlen(errMsg), 0);
    } else if (sendmessage[0] != '\0') {
		// send headers (and potential error body) as long as they were built ok
		if (send(sockfd, sendmessage, current_len, 0) < 0) { // Changed: Send current_len bytes
			perror("send header");
			// pthread_exit(NULL); // Changed: Avoid exiting thread directly, allow cleanup
		}
	}

    // Changed: Check urifd >= 0 before trying to read/close
	// if (statcode == 200 && (strcmp(req->method, "HEAD") != 0)) {
    // Added: Check 'ok' flag before sending body
    if (ok && statcode == 200 && urifd >= 0 && (strcmp(req->method, "HEAD") != 0)) {
		// send the requested file as long as there's no error and the
		// request wasn't just for the headers
		int readbytes;
        char filebuf[BUFSIZE]; // Added: Use a separate buffer for reading file content

		// while (readbytes = read(urifd, sendmessage, BUFSIZE)) { // Changed: Read into filebuf, not sendmessage
        while ((readbytes = read(urifd, filebuf, sizeof(filebuf))) > 0) { // Changed: Check > 0
			// if (readbytes < 0) { // Moved check after read
			// 	perror("read");
			// 	pthread_exit(NULL);
			// }
			// if (send(sockfd, sendmessage, readbytes, 0) < 0) { // Changed: Send filebuf
            if (send(sockfd, filebuf, readbytes, 0) < 0) {
				perror("send body");
				// pthread_exit(NULL); // Changed: Avoid exiting thread directly
                break; // Stop trying to send if error occurs
			}
		}
        // Added: Check for read error after the loop
        if (readbytes < 0) {
            perror("read file");
        }
	}

    // --- Cleanup --- // Added: Cleanup section comment
    if (urifd >= 0) {
        close(urifd); // Added: Close file descriptor if it was opened
    }
    free(imstime); // Added: Free memory allocated by get_header (safe to call free on NULL)

    // NOTE: Memory allocated by parsereq (req->method, req->uri, etc.)
    // is NOT freed here. It should be freed in the calling function (data_thread)
    // after send_response returns, as was the original design pattern.

    return 0; // Added: Return value (though function is void* in thread context)
}

// --- End of Modified Section ---


void *data_thread(void *sockfd_ptr) {

	int sockfd = *(int *) sockfd_ptr;
	const int BUFSIZE = 5; // Original buffer size for recv
	char recvmessage[BUFSIZE];
	char *headerstr = NULL;
	char *newheaderstr = NULL;
	int recvbytes = 0;
	int curheadlen = 0; // Unused in original logic shown
	int totalheadlen = 0;
	httpreq_t req; // No initialization in original
	int statcode = 200;
	int done = 0;
	int seen_header = 0;
	char *header_end;
	int content_length = 0;
	char *qstr; // Unused in original logic shown

	free(sockfd_ptr); // we have the int value out of this now
	recvmessage[BUFSIZE - 1] = '\0'; // mark end of "string"

	/* Read incoming client message from the socket */
	while(!done && (recvbytes = recv(sockfd, recvmessage, BUFSIZE - 1, 0))) {
		if (recvbytes < 0) {
			perror("recv");
			pthread_exit(NULL);
		}


		recvmessage[recvbytes] = '\0';

		if (seen_header) {
			// getting the entity body
			content_length -= recvbytes;
			if (content_length <= 0) done = 1;

		} else {

			newheaderstr = (char *) malloc((totalheadlen + recvbytes + 1) * sizeof(char));
			newheaderstr[totalheadlen + recvbytes] = '\0';
			memcpy(newheaderstr, headerstr, totalheadlen);
			memcpy(newheaderstr + totalheadlen, recvmessage, recvbytes);

			if (headerstr != NULL) {
				free(headerstr);
			}

			headerstr = newheaderstr;
			totalheadlen += recvbytes;

			header_end = strstr(headerstr, "\r\n\r\n");

			if (header_end) {
				seen_header = 1;
				header_end[2] = '\0';

				if (parsereq(&req, headerstr) != 0) {
					statcode = 400;
				}

				if (strcmp(req.method, "POST") == 0) {

					// grab the body length
					char *clenstr = get_header(&req, "Content-Length");

					if (clenstr) {

						content_length = atoi(clenstr) - ((headerstr + totalheadlen) - header_end - 4);

						if (content_length <= 0) {
							done = 1;
						}

						free(clenstr);

					} else {

						statcode = 400; // bad request -- no content length
						done = 1;
					}

				} else {

					// This isn't a POST, so there's no entity body
					done = 1;

					if (strcmp(req.method, "GET") != 0
						&& strcmp(req.method, "HEAD") != 0) {

						statcode = 501; // unknown request method
					}

				}
			} // end of "if (header_end)"
		}
	} // end of recv while loop

	// used to deref a NULL pointer here... :(
	if (headerstr != NULL) {
		printf("%s\n", headerstr);
		free(headerstr);
	}

	send_response(sockfd, &req, statcode);
    // Added: Free memory allocated by parsereq after send_response is done using it
    if (req.method && req.method[0] != '\0') free(req.method);
    if (req.uri && req.uri[0] != '\0') free(req.uri);
    // Check if version was allocated (not the "0.9" literal) before freeing
    if (req.version && strcmp(req.version, "0.9") != 0) free(req.version);
    if (req.headers && req.headers[0] != '\0') free(req.headers);

	close(sockfd);

	return NULL;

}

// Added: Function prototype for data_thread before main
void *data_thread(void *sockfd_ptr);


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
