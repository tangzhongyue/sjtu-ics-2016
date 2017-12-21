/*
 * proxy.c - CS:APP Web proxy
 *
 * 唐仲乐 515030910312 tangzhongyue@sjtu.edu.cn
 * 
 * IMPORTANT: Give a high level description of your code here. You
 * must also provide a header comment at the beginning of each
 * function that describes what that function does.
 */ 

#include "csapp.h"

/*
 * Function prototypes
 */
void unix_error_w(char *msg);
ssize_t Rio_readn_w(int fd, void *usrbuf, size_t n);
void Rio_writen_w(int fd, void *usrbuf, size_t n);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);
int parse_uri(char *uri, char *target_addr, char *path, int  *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);
void doit(int fd, struct sockaddr_in *sockaddr);
void *thread(void *vargp);

/* Global variables */
struct arg{                     /* The arg struct, used for passing args through threads */
    int fd;                     /* Client fd */
    struct sockaddr_in sockaddr;/* clientaddr */
};
sem_t mutex;                    /* Used for the lock of writing into the logfile */
FILE* logfile;                  /* The file pointer to the logfile */
/* End global variables */
/* 
 * main - Main routine for the proxy program 
 */
int main(int argc, char **argv)
{
    int listenfd, connfd, port;
    struct sockaddr_in clientaddr;
    socklen_t clientlen;
    pthread_t tid;
    logfile = fopen("proxy.log", "a");
    /* Check arguments */
    if (argc != 2) {
       fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
       exit(0);
    }
    /* Ignore SIGPIPE signals */
    signal(SIGPIPE, SIG_IGN);
    /* Initalize mutex */
    Sem_init(&mutex, 0, 1);
    port = atoi(argv[1]);
    listenfd = Open_listenfd(port);  
    while(1){
        struct arg *vargp = (struct arg *)malloc(sizeof(struct arg)); 
        clientlen = sizeof(clientaddr);  
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); 
        vargp->fd = connfd;
        vargp->sockaddr =  clientaddr;
        pthread_create(&tid, NULL, thread, (void *)vargp);
    }  
    fclose(logfile);
    exit(0);
}


/*
 * parse_uri - URI parser
 * 
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char *uri, char *hostname, char *pathname, int *port)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0) {
    hostname[0] = '\0';
    return -1;
    }
       
    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';
    
    /* Extract the port number */
    *port = 80; /* default */
    if (*hostend == ':')   
    *port = atoi(hostend + 1);
    
    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin != NULL) {
    strcpy(pathname, pathbegin);
    }

    return 0;
}

/*
 * format_log_entry - Create a formatted log entry in logstring. 
 * 
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, 
              char *uri, int size)
{
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a, b, c, d;

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    /* 
     * Convert the IP address in network byte order to dotted decimal
     * form. Note that we could have used inet_ntoa, but chose not to
     * because inet_ntoa is a Class 3 thread unsafe function that
     * returns a pointer to a static variable (Ch 13, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;


    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %d.%d.%d.%d %s %d\n", time_str, a, b, c, d, uri, size);
}

/* unix-error that only returns */
void unix_error_w(char *msg) 
{
    fprintf(stderr, "%s: %s\n", msg, strerror(errno));
}

/***************************************************************************
 * Wrappers for robust I/O routines that don't exit after detecting an error
 ***************************************************************************/
void Rio_writen_w(int fd, void *usrbuf, size_t n) 
{
    if (rio_writen(fd, usrbuf, n) != n)
    unix_error_w("Rio_writen error");
    return;
}

ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen) 
{
    ssize_t rc;

    if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0) {
        unix_error_w("Rio_readlineb error");
        return 0;
    }
    return rc;
}

ssize_t Rio_readn_w(int fd, void *ptr, size_t nbytes) 
{
    ssize_t n;
  
    if ((n = rio_readn(fd, ptr, nbytes)) < 0){
        unix_error_w("Rio_readn error");     
        return 0;  
    }
    return n;
}
/* End wrappers */

/* 
 * doit - Handle an HTTP-related action
 */
void doit(int fd, struct sockaddr_in *sockaddr){
    rio_t client_rio, server_rio;/* For rio */  
    char buf[MAXLINE], /* Buffer */
         method[MAXLINE], uri[MAXLINE], version[MAXLINE], /* Store request information */ 
         hostname[MAXLINE], path[MAXLINE], /* Store host and path */
         logstring[MAXLINE], /* Store log record */
         uri_copy[MAXLINE]; /* Store uri for log recording, since parse_uri modify the uri */
    int serverfd, port;
    char *end="</html>\0"; /* Used for handling the end of file which ends without a newline */
    ssize_t n, size = 0; /* n for recording bytes read each line, size for recording bytes read each time */
    Rio_readinitb(&client_rio, fd);   
    Rio_readlineb_w(&client_rio, buf, MAXLINE);   
    sscanf(buf, "%s %s %s", method, uri, version); 
    strncpy(uri_copy, uri, sizeof(uri));
    if (strcasecmp(method,"GET")) {      
        return;  
    }    
    parse_uri(uri, hostname, path, &port); 
    sprintf(buf,"GET %s HTTP/1.0\r\nHost: %s\r\n\r\n",path,hostname);
    serverfd = Open_clientfd(hostname, port) ;
    Rio_readinitb(&server_rio, serverfd);  
    Rio_writen_w(serverfd, buf, sizeof(buf));  

    while ((n = Rio_readlineb_w(&server_rio,buf,MAXLINE)) != 0) {   
        if(n == MAXLINE){   /* Handling the problem occured when the size of line is larger than MAXLINE */
            Rio_writen_w(fd, buf, MAXLINE - 1);
            size += MAXLINE - 1;
        }
        else if(!strncmp(buf, end, 8)){ /* Handling the end of file which ends without a newline */
            Rio_writen_w(fd, buf, 7);
            size += 7;
        }
        else {  /* Normal */
            Rio_writen_w(fd, buf, n);
            size += n;
        }
    }
    Close(serverfd);
    /* Writing into the logfile, lock when writing */
    format_log_entry(logstring, sockaddr, uri_copy, size);
    P(&mutex);
    fprintf(logfile, "%s", logstring);
    V(&mutex);
}

/*
 * thread - Thread routine
 */
void *thread(void *vargp){
    struct arg *tmp = (struct arg*)vargp;
    int connfd = tmp->fd;
    struct sockaddr_in clientaddr = tmp->sockaddr;
    pthread_detach(pthread_self());
    free(vargp);
    doit(connfd, &clientaddr);
    Close(connfd);
    return NULL;
}



