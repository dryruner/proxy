#include <stdio.h>
#include <stdlib.h>
#include "csapp.h"
#include "cache.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

sem_t mutex;
extern sem_t w_mutex; 
extern sem_t cnt_mutex;
extern sem_t mtime_mutex;

typedef struct main_args{
	Cache* p_Cache;
	int connfd;	
}main_args;

/* function prototypes */
void doit(int, struct Cache*);
void clienterror(int, char*, char*, char*, char*);
void parse_url(char*, char*, char*, char*)__attribute__((always_inline));
void parse_build_requesthead(rio_t*, char*);
void* thread(void*);
int Rio_writen_my(int, void*, size_t);  // Wrapper function to deal with broken pipe(caused by Rio_writen) error
int Rio_readnb_my(rio_t*, void*, size_t) ; // Wraper function to deal with reset by peer(cause by Rio_readnb) error
struct hostent *Gethostbyname_my(const char*);// thread-safe Wrapper function
int open_clientfd_my(char*, int);
int Open_clientfd_my(char*, int, int); // thread-safe Wrapper function

//void sig_int(int);


int main(int argc, char* argv[])
{
	int listenfd,  port;
	socklen_t clientlen;
	struct sockaddr_in clientaddr;
	pthread_t tid;

	if(argc != 2)
	{
		fprintf(stderr, "Usage: %s <port>\n",argv[0]);
		exit(1);
	}

	Signal(SIGPIPE, SIG_IGN);
//	Signal(SIGINT,sig_int);

	/* construct and initialize header of cache linked list */
	
	Cache* p_Cache = Malloc(sizeof(struct Cache));
	p_Cache->obj_size = 0;
	p_Cache->last_access = 0;
	p_Cache->p_url = NULL;
	p_Cache->p_next = NULL;
	p_Cache->response_body = NULL;

	Sem_init(&mutex,0,1);
	Sem_init(&w_mutex,0,1);
	Sem_init(&cnt_mutex,0,1);
	Sem_init(&mtime_mutex,0,1);

	port = atoi(argv[1]);
	clientlen = sizeof(clientaddr);
	
	listenfd = Open_listenfd(port);

	main_args* p_arg;
	while(1)
	{
		p_arg = Malloc(sizeof(struct main_args));
		(*p_arg).p_Cache = p_Cache;
		(*p_arg).connfd = Accept(listenfd, (SA*)&clientaddr, &clientlen);
		Pthread_create(&tid, NULL, thread, (void*)p_arg);
	}
    return 0;
}

void* thread(void* arg)
{
	Pthread_detach(Pthread_self());
	int connfd = (*(struct main_args*)arg).connfd;
	Cache* p_Cache = (*(struct main_args*)arg).p_Cache;
	Free(arg);
	doit(connfd, p_Cache);
	Close(connfd);
	return NULL;
}

void doit(int fd, Cache* p_Cache)
{
	rio_t rio;
	/* proxy receives:  GET http://www.cmu.edu:8080/hub/index.html HTTP/1.1  */
	char buf[MAXLINE], method[10], url[MAXLINE], uri[MAXLINE], host[MAXLINE], response[MAXBUF];
	/* proxy should send:  GET /hub/index.html HTTP/1.0  to server's 8080 port */
	char request_line[MAXLINE] = ""; //request_line built by proxy that will be sent to server
	char request_header[MAXLINE] = ""; // header bulit by proxy that will be sent to server
	char request_port[10] = ""; // client assigns a port
	char version[] = "HTTP/1.0";
	char save_buf[MAX_OBJECT_SIZE + 1] = ""; // save temp_data from server, no bigger than MAX_OBJ_SIZE
	int clientfd, port;
	int read_size;
	Cache* p_is_hit; // if hit cache, this pointer holds the addrress of cache_block; if not hit, return NULL
	Rio_readinitb(&rio, fd);
	if(rio_readlineb(&rio, buf, MAXLINE) < 0) // read request line
	{
		fprintf(stderr, "rio_readlineb error: %s\n",strerror(errno));
		return;
	}
	sscanf(buf, "%s %s",method, url);

	if(strcasecmp(method, "GET"))
	{
		clienterror(fd,method, "501", "Not Implemented","Proxy doesn't implement this request type");
		Close(fd);
		Pthread_exit(NULL);
	}
	parse_url(url, request_port, host, uri);
	port = !strlen(request_port) ? 80 : atoi(request_port);
	sprintf(url,"%s:%d%s", host, port, uri); // rebuild url, using url as a key to search cache
//	fprintf(stderr, "url: %s\n",url);	
	if( Search_and_Transfer(url, p_Cache,fd,(void**)&p_is_hit) < 0)
		return;
	if(p_is_hit == NULL) // if not hit cache, proxy connect to server, receive and build cache block
	{
		fprintf(stderr ,"miss\n");
		sprintf(request_line, "%s %s %s\r\n", method, uri, version); // build request line
		parse_build_requesthead(&rio, request_header); // proxy get and parse request head from client and build header for the server
		if((clientfd = Open_clientfd_my(host, port, fd)) < 0)
			return;

		printf("%s: %d", host, port);

		/* 可优化 */
		if(Rio_writen_my(clientfd, request_line, strlen(request_line)) < 0) // proxy send request-line to server
		{
			Close(clientfd);
			return;
		}
		if(Rio_writen_my(clientfd, request_header, strlen(request_header)) < 0)// proxy send request-header to server
		{
			Close(clientfd);
			return;
		}
	
		Rio_readinitb(&rio, clientfd);
		/* get server's response and save in save_buf temporarily */
//		int total_size = 0;
		if((read_size = Rio_readnb_my(&rio, response, MAXBUF)) > 0)
		{
			if(read_size <= MAX_OBJECT_SIZE) // object should be cached and send back to client
			{
				fprintf(stderr, "should add cache!\n");
				if(Rio_writen_my(fd, save_buf, read_size) < 0) // sent back to client
				{
					Close(clientfd);
					return;
				}
				Write_and_Update(p_Cache, read_size, url, save_buf); // write to cache and update access time
			}
			else // should not cache, transfer it to client directly
			{
				fprintf(stderr, "shouldn't cache!\n");
				if(Rio_writen_my(fd, save_buf, read_size) < 0)
				{
					Close(clientfd);
					return;
				}
				while((read_size = Rio_readnb_my(&rio, save_buf, MAX_OBJECT_SIZE+1)) > 0)
					if(Rio_writen_my(fd, save_buf, read_size) < 0)
						break;
			}
		}
		Close(clientfd);
	}
//	else
//		Rio_writen_my(fd, p_is_hit->response_body, p_is_hit->obj_size);
	
}


/* parse_url parses sth like http://www.cmu.edu:8080/hub/index.html ; and fills the results in host[] and uri[]*/
__attribute__((always_inline))
void parse_url(char* url, char* p_port, char* host, char* uri)
{
	char* ptr, *p;
	if((ptr = strstr(url, "://")) == NULL) // if brower doesn't append http:// head, append it manually
	{
		char temp[MAXLINE];
		strcpy(temp, "http://");
		strcat(temp, url);
		strcpy(url, temp);
		ptr = url + 4;
	}
	ptr += 3; // cut http:// from request line
	url = ptr;
	if((ptr = strstr(url, "/")) == NULL) // if brower doesn't append '/', then append it manually
	{
		int len = strlen(url);
		url[len] = '/';
		url[len+1] = '\0';
		ptr = url + len;
	}
	p = ptr;
	while(*(p + 1) == '/'){
		p += 1;
	}
	strcpy(uri, p); // construct uri 
	*ptr = '\0';
	if((ptr = strstr(url, ":")) != NULL) // if client provide a port
	{	
		*ptr = '\0';
		strcpy(p_port, ptr+1);
	}
	strcpy(host, url); // get Host: www.cmu.edu
}


/* proxy parses client's request header and build header to be sent to server */
void parse_build_requesthead(rio_t* rp, char* header)
{
	//char* ptr;
	char buf[MAXLINE];
	Rio_readlineb(rp, buf, MAXLINE);
	int ua = 0, ac = 0, ae = 0, co = 0, pc = 0;

	while(strcmp(buf, "\r\n") && strcmp(buf, "\n"))
	{
		/*
		if((ptr = strstr(buf, ":")) == NULL)
		{	
			fprintf(stderr, "request head error\n");
			Pthread_exit(NULL);
		}
		*ptr = '\0';
		*/
		if(strstr(buf, "User-Agent")){
			sprintf(header, "%sUser-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n",header);
			ua = 1;
		}
		else if(strstr(buf, "Accept")){
			sprintf(header, "%sAccept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n",header);
			ac = 1;
		}
		else if(strstr(buf, "Accept-Encoding")){
			sprintf(header, "%sAccept-Encoding: gzip, deflate\r\n",header);
			ae = 1;
		}
		else if(strstr(buf, "Connection")){
			sprintf(header, "%sConnection: close\r\n",header);
			co = 1;
		}
		else if(strstr(buf, "Proxy-Connection")){
			sprintf(header, "%sProxy-Connection: close\r\n",header);
			pc = 1;
		}
		else
		{
			//*ptr = ':';  other request headers, proxy forward them unchanged
			sprintf(header, "%s%s", header,buf);
		}
		Rio_readlineb(rp, buf, MAXLINE);
	}

	if(!ua){
		sprintf(header, "%sUser-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n",header);
	}

	if(!ac){
		sprintf(header, "%sAccept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n",header);
	}

	if(!ae){
		sprintf(header, "%sAccept-Encoding: gzip, deflate\r\n",header);
	}

	if(!co){
		sprintf(header, "%sConnection: close\r\n",header);
	}

	if(!pc){
		sprintf(header, "%sProxy-Connection: close\r\n",header);
	}
	sprintf(header, "%s\r\n", header);
}



void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title> Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Proxy Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    if(Rio_writen_my(fd, buf, strlen(buf)) < 0) return;
    sprintf(buf, "Content-type: text/html\r\n");
    if(Rio_writen_my(fd, buf, strlen(buf)) < 0) return;
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    if(Rio_writen_my(fd, buf, strlen(buf)) < 0) return;
    if(Rio_writen_my(fd, body, strlen(body)) < 0) return;
}

int Rio_readnb_my(rio_t *rp, void *usrbuf, size_t n) 
{
	int rc;
	if((rc = rio_readnb(rp, usrbuf, n)) < 0)
	{
		fprintf(stderr, "Rio_readnb_my error: %s\n",strerror(errno));
		return -1;
	}
	return rc;
}


int Rio_writen_my(int fd, void *usrbuf, size_t n) 
{
    if (rio_writen(fd, usrbuf, n) < 0)
	{
		fprintf(stderr, "%lu, Rio_writen_my error: %s\n",pthread_self(), strerror(errno));
		return -1;
	}
	return 0;
}


int Open_clientfd_my(char* hostname, int port, int fd)
{
	int rc;
	if((rc = open_clientfd_my(hostname,port)) < 0)
	{
		if(rc == -1)
			fprintf(stderr, "Open_clientfd_my error: %s\n",strerror(errno));
		else
			clienterror(fd, hostname, "Error ", "DNS error", "Invalid domain name ");
	}
	return rc;
}

int open_clientfd_my(char *hostname, int port) 
{
    int clientfd;
    struct hostent *hp;
    struct sockaddr_in serveraddr;

    if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return -1; /* check errno for cause of error */

    /* Fill in the server's IP address and port */
    if ((hp = Gethostbyname_my(hostname)) == NULL)
		return -2; /* check h_errno for cause of error */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)hp->h_addr_list[0], 
	  (char *)&serveraddr.sin_addr.s_addr, hp->h_length);
    serveraddr.sin_port = htons(port);
	Free(hp);
    /* Establish a connection with the server */
    if (connect(clientfd, (SA *) &serveraddr, sizeof(serveraddr)) < 0)
		return -1;
    return clientfd;
}

struct hostent *Gethostbyname_my(const char *name) 
{
    struct hostent *p;
	struct hostent* q = Malloc(sizeof(struct hostent));
	P(&mutex);
    if ((p = gethostbyname(name)) == NULL)
	{
		V(&mutex);
		Free(q);
		return NULL;
	}
	*(q) = *(p); //copy
	V(&mutex);
    return q;
}


/*
void sig_int(int signo)
{
	Destroy_Cache();
}*/
