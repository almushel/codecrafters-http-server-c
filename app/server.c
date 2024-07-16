#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

typedef struct http_status {
	unsigned int version_major;
	unsigned int version_minor;
	int code;
	char* reason;
} http_status;

typedef struct http_request_header {
	char method[32];
	char target[256];
	unsigned int version_major;
	unsigned int version_minor;
} http_request_header;

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

	char request[256];
	int bytes_read;
	if ( (bytes_read = recv(client_fd, request, 256, 0)) < 0) {
		printf("Recv failed: %s \n", strerror(bytes_read));
		return 1;
	}
	
	http_request_header r_header;
	printf("Parsing request header...\n");
	if ( sscanf(request, "%s %s HTTP/%u.%u\r\n", r_header.method, r_header.target, &r_header.version_major, &r_header.version_minor) != 4) {
		printf("Invalid request header\n");
		return 1;
	}

	if (strcmp(r_header.method, "GET") != 0) {
		printf("Unsupported request method. Supported: GET\n");
		return 1;
	}

	if (r_header.version_major != 1 || r_header.version_minor != 1) {
		// Status code 505
		printf("Unsupported HTTP version %u.%u. Requires 1.1\n", r_header.version_major, r_header.version_minor);
		return 1;
	}

	printf("Sending response...\n");
	http_status status = {
		.version_major = 1,
		.version_minor = 1,
	};
	if (strcmp(r_header.target, "/") == 0) {
		status.code = 200;
		status.reason = "OK";
	} else {
		status.code = 404;
		status.reason = "Not Found";
	}

	char response[256];
	sprintf(response, "HTTP/%u.%u %u %s\r\n\r\n", status.version_major, status.version_minor, status.code, status.reason);
	send(client_fd, response, strlen(response), 0);

	close(server_fd);

	return 0;
}
