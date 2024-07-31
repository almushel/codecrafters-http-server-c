#include <stdio.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>


typedef struct str_node str_node;
struct str_node {
	char* key;
	char* val;

	str_node* next;
};

typedef struct str_map {
	str_node* data;
	size_t size;
} str_map;

typedef struct http_status {
	unsigned int version_major;
	unsigned int version_minor;
	int code;
	char* reason;
} http_status;

typedef struct http_response {
	http_status status;
	str_map headers;

	char* body;
	size_t body_size;
} http_response;

typedef struct http_request {
	char method[32];
	char target[256];
	unsigned int version_major;
	unsigned int version_minor;

	str_map headers;

	char* body;
	size_t body_size;
} http_request;

static char* serve_dir = 0;

// Really bad hash function
size_t str_hash(const char* key) {
	size_t result = 0;
	size_t len = strlen(key);

	for (int i = 0; i < len; i++) {
		result += key[i];
	}

	return result;
}

str_map str_map_new(size_t size) {
	str_map result = {
		.data = calloc(size, sizeof(str_node)),
		.size = size
	};

	return result;
}

void str_map_free(str_map* map) {
	for (int i = 0; i < map->size; i++) {
		str_node* node = map->data+i;
		while (node) {
			node->key = realloc(node->key, 0);
			node->key = realloc(node->key, 0);
		}
	}
	free(map->data);
	*map = (str_map){0};
}

void str_map_set(str_map* m, const char* key, const char* val) {
	size_t index = str_hash(key) % m->size;
	str_node* node = m->data+index;
	while (node->key && strcmp(key, node->key) != 0) {
		if (node->next == NULL) {
			node->next = malloc(sizeof(str_node));
			node = node->next;
			*node = (str_node) {0};
			break;
		}
		node = node->next;
	}

	node->key = realloc(node->key, strlen(key)+1);
	node->val = realloc(node->val, strlen(val)+1);
	strcpy(node->key, key);
	strcpy(node->val, val);
}

char* str_map_get(str_map* m, const char* key) {
	size_t index = str_hash(key) % m->size;
	str_node* node = m->data+index;
	while (node && node->key && strcmp(key, node->key) != 0) {
		node = node->next;
	}

	if (node == NULL || node->key == NULL || node->val == NULL) {
		return 0;
	}

	puts(node->key);
	return node->val;

}

int is_dir(const char* path) {
	if (path == 0 || path[0] == '\0') {
		return 0;
	}

	struct stat buf;
	stat(path, &buf);

	return S_ISDIR(buf.st_mode);
}


int handle_echo_request(http_request* request, http_response* response) {
	char* target = request->target+sizeof("/echo/")-1;
	response->body_size = strlen(target);
	response->body = malloc(request->body_size+1);
	strcpy(response->body, target);

	str_map_set(&response->headers, "Content-Type", "text/plain");

	return 1;
}

int handle_file_request(http_request* request, http_response* response) {
	char* result = 0;
	size_t result_size = 0;
	size_t result_used = 0;

	char path_buf[FILENAME_MAX+1] = {0};
	int last = 0;
	if (serve_dir) {
		strncpy(path_buf, serve_dir, FILENAME_MAX);
		last = strlen(path_buf);
		if (path_buf[last] != '/') {
			path_buf[last+1] = '/';
			path_buf[last+2] = '\0';
		}
	}
	strncat(path_buf, request->target+sizeof("/files/")-1, FILENAME_MAX-last+1);

	FILE* target_file = fopen(path_buf, "r");
	if (target_file == NULL) {
		return 0;
	} else {
		result_size = 1024;
		result = malloc(result_size);
		
		char read_buf[1024];
		size_t bytes_read;
		while ( (bytes_read = fread(read_buf, 1, 1024, target_file)) ) {
			if (result_used+bytes_read > result_size) {
				result_size *= 2;
				result = realloc(result, result_size);
			}
			strncpy(result+result_used, read_buf, result_size-result_used);
			result_used += bytes_read;
		}

		fclose(target_file);
	}

	response->body = result;
	response->body_size = result_used;

	str_map_set(&response->headers, "Content-Type", "application/octet-stream");

	return 1;
}

int handle_user_agent_request(http_request* request, http_response* response) {
	char* src = str_map_get(&request->headers, "User-Agent");
	if (src) {
		response->body_size = strlen(src);
		response->body = malloc(response->body_size+1);
		strcpy(response->body, src);

		str_map_set(&response->headers, "Content-Type", "text/plain");
	}

	return 1;
}

int handle_get_request(http_request* request, http_response* response) {
	// Handle endpoints or 404
	enum { NONE, STATUS, ECHO, FILE, USER_AGENT } response_type = NONE;

	if (strcmp("/", request->target) == 0) {
		response_type = STATUS;
	} else if (strncmp("/echo", request->target, sizeof("/echo")-1) == 0) {
		if (handle_echo_request(request, response)) {
			response_type = ECHO;
		}
	} else if (strncmp("/files", request->target, sizeof("/files")-1) == 0) {
		if (handle_file_request(request, response)) {
			response_type = FILE;
		}
	} else if (strcmp("/user-agent", request->target) == 0) {
		if (handle_user_agent_request(request, response)) {
			response_type = USER_AGENT;
		}
	} 

	if (response_type != NONE) {
		response->status.code = 200;
		response->status.reason = "OK";
	} else {
		response->status.code = 404;
		response->status.reason = "Not Found";
	}

	char content_length[128];
	if (response->body_size) {
		snprintf(content_length, 127, "%lu", response->body_size);
		str_map_set(&response->headers, "Content-Length", content_length);
	}

	return 1;
}

int handle_post_request(http_request* request, http_response* response) {
	if (strncmp("/files/", request->target, sizeof("/files/")-1) == 0) {
		char path_buf[FILENAME_MAX+1] = {0};
		if (serve_dir) {
			strncpy(path_buf, serve_dir, FILENAME_MAX-strlen(serve_dir));
		}
		strncat(path_buf, request->target+sizeof("/files/")-1, FILENAME_MAX-strlen(path_buf));
		FILE* fd = fopen(path_buf, "w");
		if (fwrite(request->body, 1, request->body_size, fd) != request->body_size) {
			// write failure	
		}
		fclose(fd);
	}

	response->status.code = 201;
	response->status.reason = "Created";

	return 1;
}

void* handle_connection(void* cfd) {
	int client_fd = (int)(long)cfd;

	char request_buf[256];
	int bytes_read;
	if ( (bytes_read = recv(client_fd, request_buf, 256, 0)) < 0) {
		printf("Recv failed: %s \n", strerror(bytes_read));
		return (void*)1;
	}

	int lines_size = 8;
	int line_count = 1;
	char** lines = malloc(sizeof(char*) * lines_size);
	lines[0] = request_buf;
	for (char* c = request_buf; c != (request_buf+bytes_read); c++) {
		if (c[0] == '\r' && c[1] == '\n') {
			if (line_count == lines_size) {
				lines_size *= 2;
				lines = realloc(lines, lines_size); 
			}

			*c = '\0';
			lines[line_count] = (c+2);
			line_count++;
		}
	}
	
	http_request request = {0};
	puts("Parsing request header...");
	if ( sscanf(lines[0], "%s %s HTTP/%u.%u\r\n", request.method, request.target, &request.version_major, &request.version_minor) != 4) {
		puts("Invalid request header");
		return (void*)1;
	}

	if (request.version_major != 1 || request.version_minor != 1) {
		// Status code 505
		printf("Unsupported HTTP version %u.%u. Requires 1.1\n", request.version_major, request.version_minor);
		return (void*)1;
	}

	request.headers = str_map_new(8);
	for (int i = 1; i < line_count-1; i++) {
		char* left = lines[i]; 
		char* right = 0;
		char c = 0;
		while (left[c] != '\0') {
			if (left[c] == ':') {
				left[c] = '\0';
				while(isspace(left[++c]));
				right = (left+c);

				break;
			}
			c++;
		}
		
		if (left && right) {
			str_map_set(&request.headers, left, right);
		}
	}

	puts("Processing header body...");
	char* content_length = str_map_get(&request.headers, "Content-Length");
	if (content_length) {
		puts(content_length);
		request.body_size = strtoul(content_length, 0, 10);
		request.body = request_buf + (bytes_read-request.body_size);
	}

	puts("Constructing response...");
	http_response response = {
		.status = {
			.version_major = 1,
			.version_minor = 1,
		},
		.headers = str_map_new(8)
	};

	if (strcmp("GET", request.method) == 0) {
		handle_get_request(&request, &response);
	} else if (strcmp("POST", request.method) == 0) {
		handle_post_request(&request, &response);
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
	for(int i = 0; i < response.headers.size; i++) {
		str_node* node = response.headers.data+i;
		while (node && node->key != NULL && node->val != NULL) {
			snprintf(response_str+r_used, r_len-r_used, "%s: %s\r\n", node->key, node->val);
			r_used = strlen(response_str);
			node = node->next;
		}
	}
	strncat(response_str, "\r\n", r_len-r_used);
	r_used = strlen(response_str);
	
	// Print body
	if (response.body) {
		strncat(response_str, response.body, r_len-r_used);
		r_used = strlen(response_str);
	}

	puts("Sending response...");
	puts(response_str);
	send(client_fd, response_str, strlen(response_str), 0);
	close(client_fd);
	puts("Response sent");

	free(lines);
	str_map_free(&request.headers);
	str_map_free(&response.headers);
	if (response.body) free(response.body);

	return (void*)0;
}

int main(int argc, char * argv[]) {
	// Disable output buffering
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	for (int i = 1; i < argc; i++) {
		if (strncmp("--directory", argv[i], sizeof("--directory")-1) == 0) {
			if (argc > i+1 && is_dir(argv[i+1])) {
				serve_dir = argv[i+1];
			}
		}
	}

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
	while(1) {
		puts("Waiting for a client to connect...");
		if (listen(server_fd, connection_backlog) != 0) {
			printf("Listen failed: %s \n", strerror(errno));
			return 1;
		}

		client_addr_len = sizeof(client_addr);
		int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, &client_addr_len);
		if (client_fd == -1) continue;

		puts("Client connected");
		
		pthread_t thread;
		pthread_create(&thread, 0, handle_connection, (void*)(long)client_fd);
		pthread_detach(thread);
	}

	close(server_fd);

	return 0;
}
