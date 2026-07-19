#include "csapp.h"
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

static char user_agent_hdr[] = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

typedef struct cache_object {
    char *key;
    char *data;
    size_t size;
    unsigned long recent;
    struct cache_object *next;
} cache_object;

static cache_object *cache;
static size_t cache_size;
static unsigned long cache_clock;
static pthread_rwlock_t cache_lock = PTHREAD_RWLOCK_INITIALIZER;

void doit(int clientfd);
void forward_request(rio_t *client_rp, int serverfd);
void parse_uri(char *uri, char *hostname, char *path, char *port);
void clienterror(int fd, char *cause, char *errnum,
         char *shortmsg, char *longmsg);
void *thread(void *vargp);
int cache_get(const char *key, char *data, size_t *size);
void cache_put(const char *key, const char *data, size_t size);
void cache_evict_lru(void);

int main(int argc, char **argv)
{
    int listenfd, *connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    Signal(SIGPIPE, SIG_IGN);
    listenfd = Open_listenfd(argv[1]);

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Malloc(sizeof(int));
        *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Pthread_create(&tid, NULL, thread, connfd);
    }
    return 0;
}

void *thread(void *vargp)
{
    Pthread_detach(pthread_self());

    int connfd = *((int *)vargp);
    Free(vargp);

    doit(connfd);
    Close(connfd);

    return NULL;
}

void doit(int clientfd)
{
    char buf[MAXLINE], request[2 * MAXLINE];
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE], port[MAXLINE];
    char key[3 * MAXLINE], object[MAX_OBJECT_SIZE];
    rio_t client_rio, server_rio;
    int serverfd, cacheable = 1;
    ssize_t n;
    size_t object_size = 0;

    /* Read request line and headers */
    Rio_readinitb(&client_rio, clientfd);
    if (!Rio_readlineb(&client_rio, buf, MAXLINE))
        return;
    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET")) {
        clienterror(clientfd, method, "501", "Not Implemented",
                    "Proxy does not implement this method");
        return;
    }
    parse_uri(uri, hostname, path, port);
    snprintf(key, sizeof(key), "%s:%s%s", hostname, port, path);
    if (cache_get(key, object, &object_size)) {
        Rio_writen(clientfd, object, object_size);
        return;
    }

    serverfd = Open_clientfd(hostname, port);

    /* Build proxy request headers */
    snprintf(request, sizeof(request), "GET %s HTTP/1.0\r\n", path);
    Rio_writen(serverfd, request, strlen(request));
    snprintf(request, sizeof(request), "Host: %s\r\n", hostname);
    Rio_writen(serverfd, request, strlen(request));
    Rio_writen(serverfd, user_agent_hdr, strlen(user_agent_hdr));
    Rio_writen(serverfd, "Connection: close\r\n",
              strlen("Connection: close\r\n"));
    Rio_writen(serverfd, "Proxy-Connection: close\r\n",
              strlen("Proxy-Connection: close\r\n"));

    /* Forward client request */
    forward_request(&client_rio, serverfd);

    /* Forward server request */
    Rio_readinitb(&server_rio, serverfd);
    while ((n = Rio_readnb(&server_rio, buf, MAXBUF)) > 0) {
        Rio_writen(clientfd, buf, n);
        if (cacheable && object_size + (size_t)n <= MAX_OBJECT_SIZE) {
            memcpy(object + object_size, buf, n);
            object_size += n;
        }
        else {
            cacheable = 0;
        }
    }
    Close(serverfd);
    if (cacheable)
        cache_put(key, object, object_size);
}

void forward_request(rio_t *client_rp, int serverfd)
{
    char buf[MAXLINE];

    while (Rio_readlineb(client_rp, buf, MAXLINE) > 0) {
        if (!strncasecmp(buf, "\r\n", strlen("\r\n")))
            break;

        if (!strncasecmp(buf, "Host:", strlen("Host:")))
            continue;
        if (!strncasecmp(buf, "User-Agent:", strlen("User-Agent:")))
            continue;
        if (!strncasecmp(buf, "Connection:", strlen("Connection:")))
            continue;
        if (!strncasecmp(buf, "Proxy-Connection:", strlen("Proxy-Connection:")))
            continue;

        Rio_writen(serverfd, buf, strlen(buf));
    }
    Rio_writen(serverfd, "\r\n", strlen("\r\n"));
    return;
}

void parse_uri(char *uri, char *hostname, char *path, char *port)
{
    char *head;
    char *tail;

    head = strstr(uri, "//");
    head += 2;
    if ((tail = strchr(head, ':')) == NULL) {
        strcpy(port, "80");
        tail = strchr(head, '/');
        *tail = '\0';
        strcpy(hostname, head);
    }
    else {
        *tail++ = '\0';
        strcpy(hostname, head);
        head = tail;
        tail = strchr(head, '/');
        *tail = '\0';
        strcpy(port, head);
    }
    *tail = '/';
    strcpy(path, tail);
    return;
}

int cache_get(const char *key, char *data, size_t *size)
{
    cache_object *object;
    int found = 0;

    pthread_rwlock_rdlock(&cache_lock);
    for (object = cache; object; object = object->next) {
        if (!strcmp(object->key, key)) {
            memcpy(data, object->data, object->size);
            *size = object->size;
            found = 1;
            break;
        }
    }
    pthread_rwlock_unlock(&cache_lock);

    if (!found)
        return 0;

    pthread_rwlock_wrlock(&cache_lock);
    for (object = cache; object; object = object->next) {
        if (!strcmp(object->key, key)) {
            object->recent = ++cache_clock;
            break;
        }
    }
    pthread_rwlock_unlock(&cache_lock);
    return 1;
}

void cache_put(const char *key, const char *data, size_t size)
{
    cache_object *object;

    if (size == 0 || size > MAX_OBJECT_SIZE)
        return;

    pthread_rwlock_wrlock(&cache_lock);
    for (object = cache; object; object = object->next) {
        if (!strcmp(object->key, key)) {
            object->recent = ++cache_clock;
            pthread_rwlock_unlock(&cache_lock);
            return;
        }
    }

    while (cache && cache_size + size > MAX_CACHE_SIZE)
        cache_evict_lru();

    object = Malloc(sizeof(*object));
    object->key = Malloc(strlen(key) + 1);
    object->data = Malloc(size);
    strcpy(object->key, key);
    memcpy(object->data, data, size);
    object->size = size;
    object->recent = ++cache_clock;
    object->next = cache;
    cache = object;
    cache_size += size;
    pthread_rwlock_unlock(&cache_lock);
}

void cache_evict_lru(void)
{
    cache_object *object, *previous = NULL;
    cache_object *victim = cache, *victim_previous = NULL;

    for (object = cache; object; object = object->next) {
        if (object->recent < victim->recent) {
            victim = object;
            victim_previous = previous;
        }
        previous = object;
    }

    if (victim_previous)
        victim_previous->next = victim->next;
    else
        cache = victim->next;
    cache_size -= victim->size;
    Free(victim->key);
    Free(victim->data);
    Free(victim);
}

void clienterror(int fd, char *cause, char *errnum,
		 char *shortmsg, char *longmsg)
{
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Tiny Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor=""ffffff"">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}
