#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

typedef struct key_val {
	char* key;
	char* val;
} key_val;

typedef struct http_status {
	unsigned int version_major;
	unsigned int version_minor;
	int code;
	char* reason;
} http_status;

typedef struct http_response {
	http_status status;
	char** headers;
	int headers_size, num_headers;
	char* body;
} http_response;

typedef struct http_request {
	char method[32];
	char target[256];
	unsigned int version_major;
	unsigned int version_minor;

	key_val * headers;
	int headers_size;
	int num_headers;

	char* body;
} http_request;


int main() {
	// Disable output buffering
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	int server_fd;
	socklen_t client_addr_len;
	struct sockaddr_in client_addr;

	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		printf("Socket creation failed: %s...\n", strerror(errno));
		return 1;
	}

	// Since the tester restarts your program quite often, setting SO_REUSEADDR
	// ensures that we don't run into 'Address already in use' errors
	 int reuse = 1;
	 if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
		printf("SO_REUSEADDR failed: %s \n", strerror(errno));
		return 1;
	 }

	struct sockaddr_in serv_addr = {
		.sin_family = AF_INET ,
		.sin_port = htons(4221),
		.sin_addr = { htonl(INADDR_ANY) },
	};

	if (bind(server_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0) {
		printf("Bind failed: %s \n", strerror(errno));
		return 1;
	}

	int connection_backlog = 5;
	if (listen(server_fd, connection_backlog) != 0) {
		printf("Listen failed: %s \n", strerror(errno));
		return 1;
	}

	printf("Waiting for a client to connect...\n");

	client_addr_len = sizeof(client_addr);
	int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, &client_addr_len);
	printf("Client connected\n");

	char request_buf[256];
	int bytes_read;
	if ( (bytes_read = recv(client_fd, request_buf, 256, 0)) < 0) {
		printf("Recv failed: %s \n", strerror(bytes_read));
		return 1;
	}

	int lines_size = 8;
	char** lines = malloc(sizeof(char*) * lines_size);
	lines[0] = request_buf;
	int line_count = 1;
	for (char* c = request_buf; c != (request_buf+bytes_read); c++) {
		if (c[0] == '\r' && c[1] == '\n') {
			if (line_count == lines_size) {
				lines_size *=2;
				lines = realloc(lines, sizeof(char*) * lines_size);
			}
			*c = '\0';
			lines[line_count] = (c+2);
			line_count++;
		}
	}
	
	http_request request = {0};
	printf("Parsing request header...\n");
	if ( sscanf(lines[0], "%s %s HTTP/%u.%u\r\n", request.method, request.target, &request.version_major, &request.version_minor) != 4) {
		printf("Invalid request header\n");
		return 1;
	}

	if (strcmp(request.method, "GET") != 0) {
		printf("Unsupported request method. Supported: GET\n");
		return 1;
	}

	if (request.version_major != 1 || request.version_minor != 1) {
		// Status code 505
		printf("Unsupported HTTP version %u.%u. Requires 1.1\n", request.version_major, request.version_minor);
		return 1;
	}

	// NOTE: lines[line_count-1] should be the request body
	
	request.headers_size = 8;
	request.headers = malloc(sizeof(key_val)*request.headers_size);
	for (int i = 1; i < line_count-1; i++) {
		char* left = lines[i]; 
		char* right = 0;	
		char c = 0;
		while (left[c] != '\0') {
			if (left[c] == ':') {
				while(isspace(left[++c]));

				left[c-1] = '\0';
				right = (left+c);

				break;
			}
			c++;
		}
		
		if (left && right) {
			if (request.num_headers == request.headers_size) {
				request.headers_size *= 2;
				request.headers = realloc(request.headers, request.headers_size);
			}
			request.headers[request.num_headers-1] = (key_val) {
				.key = left,
				.val = right
			};
			request.num_headers++;
			// process header key value pair left: right
		}
	}

	http_response response = {
		.status = {
			.version_major = 1,
			.version_minor = 1,
		},
	};

	// Handle ehdpoints or 404
	if (strcmp("/", request.target) == 0) {
		response.status.code = 200;
		response.status.reason = "OK";
	} else if (strncmp("/echo", request.target, strlen("/echo")) == 0) {
		response.status.code = 200;
		response.status.reason = "OK";

		response.body = request.target+strlen("/echo/");
	} else if (strcmp("/user-agent", request.target) == 0) {
		response.status.code = 200;
		response.status.reason = "OK";

		for (int i = 0; i < request.num_headers; i++) {
			if (strncmp("User-Agent", request.headers[i].key, strlen("User-Agent")) == 0) {
				response.body = request.headers[i].val;
				break;
			}
		}
	} else {
		response.status.code = 404;
		response.status.reason = "Not Found";
	}

	if (response.body) {
		response.num_headers = response.headers_size = 2;
		response.headers = malloc(sizeof(char*) * response.headers_size);
		response.headers[0] = "Content-Type: text/plain\r\n";
		char content_length[128];
		sprintf(content_length, "Content-Length: %lu\r\n", strlen(response.body));
		response.headers[1] = content_length;
	}

	// Print status line
	int r_len = 512;
	int r_used = 0;
	char response_str[r_len];
	r_used = sprintf(
		response_str, "HTTP/%u.%u %u %s\r\n",
		response.status.version_major,
		response.status.version_minor,
		response.status.code,
		response.status.reason
	);

	//Print headers
	for(int i = 0; i < response.num_headers; i++) {
		strncat(response_str, response.headers[i], r_len-r_used);
		r_used = strlen(response_str);
	}
	strncat(response_str, "\r\n", r_len-r_used);
	r_used = strlen(response_str);
	
	// Print body
	if (response.body) {
		strncat(response_str, response.body, r_len-r_used);
		r_used = strlen(response_str);
		printf("%s\n", response_str);
	}

	printf("Sending response...\n");
	send(client_fd, response_str, strlen(response_str), 0);

	close(server_fd);

	return 0;
}
