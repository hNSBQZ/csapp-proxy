#include <stdio.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define CACHE_BLOCK_SIZE sizeof(cache_t)
#define CACHE_HEAD_SIZE sizeof(cache_head)

#define NTHREADS 4
#define SBUFSIZE 16

#define Err int
#define ERR 1
#define NIL 0
#define BOOL int
#define TRUE 1
#define FALSE 0

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr="Connection: close\r\n";
static const char *Proxy_connection_hdr="Proxy-Connection: close\r\n";

typedef struct uriData
{
    char host_name[MAXLINE];
    char port[MAXLINE];
    char path[MAXLINE];
}UriData;

typedef struct SBUF{
    int *buf;
    int n;
    int front;
    int rear;
    sem_t mutex;
    sem_t slots;
    sem_t items;
}sbuf_t;

typedef struct CACHE{
    char *content;//实际内容
    size_t size;
    struct CACHE *next_content;
}cache_t;//实际存储用的块

typedef struct CACHEHEAD
{
    struct CACHEHEAD *next;//下一个信息节点
    cache_t *last;//该url头下最后一个信息节点，方便尾插
    cache_t *content;//指向实际存储的块
    char url[MAXLINE];
    size_t total_size;
    int time_stamp;

}cache_head,*cache_list;//信息节点

//读者写者锁
typedef struct _rwlock_t { 
    sem_t lock; // binary semaphore (basic lock) 
    sem_t writelock; // used to allow ONE writer or MANY readers 
    int readers; // count of readers reading in critical section 
} rwlock_t;

//basic
void doit(int fd);
Err parse_uri(char *uri,UriData *uri_data);
Err read_requesthdr(rio_t *rp,UriData *uri_data);
void send_request(int server_fd,UriData *uri_data);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);

//operate buffer
void sbuf_init(sbuf_t *sp,int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp,int item);
int sbuf_remove(sbuf_t *sp);

//operate cache
cache_head *init_cache_head_node();
cache_t *init_cache_node();
cache_head* check_cache_exist(char *url);
void send_cache_response(cache_head *ptr,int connfd);
void store_and_send_response(server_fd,client_fd);
//将信息节点插入头节点
void insert_node_to_head(cache_head* head,cache_t* cache_node);
//将头节点链表插入cache链表
void insert_head_to_list(cache_head* head);
void free_header(cache_head* head);

void *thread(void *vargp);

//writer-reader lock
void rwlock_init(rwlock_t *rw);
void rwlock_acquire_readlock(rwlock_t *rw);
void rwlock_release_readlock(rwlock_t *rw);
void rwlock_acquire_writelock(rwlock_t *rw);
void rwlock_release_writelock(rwlock_t *rw);

sbuf_t sbuf;
size_t remain_cache_size;
cache_list cache_list_head;
rwlock_t rwlock;

int main(int argc,char **argv)
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* Check command line args */
    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(1);
    }

    sbuf_init(&sbuf,SBUFSIZE);
    remain_cache_size=MAX_CACHE_SIZE;
    cache_list_head=init_cache_head_node();
    rwlock_init(&rwlock);
    for(int i=0;i<NTHREADS;i++)
        Pthread_create(&tid,NULL,thread,NULL);

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        sbuf_insert(&sbuf,connfd);
    }
}

void *thread(void *vargp)
{
    Pthread_detach(pthread_self());
    while(1)
    {
        int client_fd=sbuf_remove(&sbuf);
        doit(client_fd);
        Close(client_fd);
    }
}

void doit(int client_fd)
{
    rio_t rio;
    UriData uri_data;
    char buf[MAXLINE],uri[MAXLINE],method[MAXLINE],version[MAXLINE];
    Err err;
    Rio_readinitb(&rio,client_fd);

    Rio_readlineb(&rio,buf,MAXLINE);

    printf("request header from client:\n");
    printf("%s",buf);
    sscanf(buf,"%s %s %s",method,uri,version);

    //只接受GET
    if(strcmp(method,"GET"))
    {
        clienterror(client_fd, "NULL", "403", "Forbidden",
			"invalid method");
        return;
    }
    
    //先看cache里有没有
    rwlock_acquire_readlock(&rwlock);
    cache_head* request_content=check_cache_exist(uri);
    if(request_content)
    {
        send_cache_response(request_content,client_fd);
        rwlock_release_readlock(&rwlock);
        return;
    }
    rwlock_release_readlock(&rwlock);

    //解析请求内容
    err=parse_uri(uri,&uri_data);
    if(err)
    {
        clienterror(client_fd, "NULL", "403", "Forbidden",
			"invalid uri");
        return;
    }

    //如果hostname未知，通过请求头查询
    if(!strcmp(uri_data.host_name,"UNKNOW"))
        err=read_requesthdr(&rio,&uri_data);
    if(err)
    {
        clienterror(client_fd, "NULL", "403", "Forbidden",
			"invalid host");
        return;
    }

    //解析完可以建立连接了
    int server_fd;
    server_fd=Open_clientfd(uri_data.host_name,uri_data.port);

    send_request(server_fd,&uri_data);

    rwlock_acquire_writelock(&rwlock);
    store_and_send_response(server_fd,client_fd);
    rwlock_release_writelock(&rwlock);

    Close(server_fd);

}

/**
 * 如下几种情况
 * http://hqz.com:8080->http://hqz.com:8080/
 * http://hqz.com/index->http://hqz.com:80/index 默认80端口，错误留给链接阶段处理
 * /index->http://hqz.com:80/index 默认80端口,host从header中读取
 * hqz.com->http://hqz.com:80/
 * hqz.com/index
*/
Err parse_uri(char *uri,UriData *uri_data)
{
    printf("parse_uri\n");
    if(!strlen(uri))
        return ERR;

    char *uri_copy;
    uri_copy=strstr(uri,"//");
    uri_copy=(uri_copy)?uri_copy+2:uri;

    char *port_pos=strstr(uri_copy,":");
    char *path_pos=strstr(uri_copy,"/");
    if(!port_pos&&path_pos)
    {
        strncpy(uri_data->host_name,uri_copy,path_pos-uri_copy);
        uri_data->host_name[path_pos-uri_copy]='\0';
        strcpy(uri_data->port,"80");
        strcpy(uri_data->path,path_pos);
    }
    else if(!path_pos&&!port_pos)
    {
        strcpy(uri_data->path,"/");
        strcpy(uri_data->port,"80");
        strcpy(uri_data->host_name,uri_copy);
    }
    else if(!path_pos)
    {
        strcpy(uri_data->port,port_pos+1);
        strcpy(uri_data->path,"/");
        strncpy(uri_data->host_name,uri_copy,port_pos-uri_copy);
        uri_data->host_name[port_pos-uri_copy]='\0';
    }
    else
    {
        strncpy(uri_data->port,port_pos+1,path_pos-port_pos-1);
        uri_data->port[path_pos-port_pos]='\0';
        strcpy(uri_data->path,path_pos);
        strncpy(uri_data->host_name,uri_copy,port_pos-uri_copy);
        uri_data->host_name[port_pos-uri_copy]='\0';
    }
    if(strlen(uri_data->host_name)==0)
        strcpy(uri_data->host_name,"UNKNOW");
    puts(uri_data->host_name);
    puts(uri_data->path);
    puts(uri_data->port);
    return NIL;
    
}

//读取请求头
Err read_requesthdr(rio_t *rp,UriData *uri_data)
{
    printf("read_requesthdr\n");
    char buf[MAXLINE];
    rio_readlineb(rp,buf,MAXLINE);
    while(strcmp(buf,"\r\n"))
    {
        char *header_pos=strstr(buf,":");
        char header[MAXLINE];
        strncpy(header,buf,header_pos-buf);
        header[header_pos-buf]='\0';
        if(!strcmp(header,"Host"))
        {
            strcpy(uri_data->host_name,header_pos+2);
            uri_data->host_name[strlen(uri_data->host_name)-2]='\0';
            return NIL;
        }
        Rio_readlineb(rp,buf,MAXLINE);
    }
    return ERR;
}

void send_request(int server_fd,UriData *uri_data)
{
    char request[MAXLINE],header_host[MAXLINE];
    memset(request,0,MAXLINE);
    memset(header_host,0,MAXLINE);
    sprintf(request,"GET %s HTTP/1.0\r\n",uri_data->path);
    sprintf(header_host,"Host: %s\r\n",uri_data->host_name);
    sprintf(request,"%s%s",request,header_host);
    sprintf(request,"%s%s",request,user_agent_hdr);
    sprintf(request,"%s%s",request,connection_hdr);
    sprintf(request,"%s%s",request,Proxy_connection_hdr);
    sprintf(request,"%s\r\n",request);
    Rio_writen(server_fd,request,strlen(request));
}

void sbuf_init(sbuf_t *sp,int n)
{
    sp->buf=Calloc(n,sizeof(int));
    sp->n=n;
    sp->front=sp->rear=0;
    Sem_init(&sp->mutex,0,1);
    Sem_init(&sp->slots,0,n);
    Sem_init(&sp->items,0,0);
}

void sbuf_deinit(sbuf_t *sp)
{
    Free(sp->buf);
}

void sbuf_insert(sbuf_t *sp,int item)
{
    P(&sp->slots);
    P(&sp->mutex);
    sp->buf[(++sp->rear)%(sp->n)]=item;
    V(&sp->mutex);
    V(&sp->items);
}

int sbuf_remove(sbuf_t *sp)
{
    int item;
    P(&sp->items);
    P(&sp->mutex);
    item=sp->buf[(++sp->front)%(sp->n)];
    V(&sp->mutex);
    V(&sp->slots);
    return item;
}

cache_head *init_cache_head_node()
{
    cache_head *head_node;
    head_node=(cache_head *)Malloc(CACHE_HEAD_SIZE);
    head_node->content=NULL;
    head_node->time_stamp=0;
    head_node->next=NULL;
    head_node->total_size=0;
    head_node->last=NULL;
    memset(head_node->url,0,MAXLINE);
    return head_node;
}

cache_t *init_cache_node()
{
    cache_t *node;
    node=Malloc(CACHE_BLOCK_SIZE);
    node->content=NULL;
    node->size=0;
    node->next_content=NULL;
    return node;
}

cache_head* check_cache_exist(char *url)
{
    cache_head *cur=cache_list_head->next;
    while(cur)
    {
        if(!strcmp(cur->url,url))
            return cur;
        cur=cur->next;
    }
    return NULL;
}

void send_cache_response(cache_head *head_ptr,int client_fd)
{
    head_ptr->time_stamp++;
    cache_t *content_ptr=head_ptr->content;
    while(content_ptr)
    {
        Rio_writen(client_fd,content_ptr->content,content_ptr->size);
        content_ptr=content_ptr->next_content;
    }
}

/**
 * 边写缓冲区边发送
*/
void store_and_send_response(int server_fd,int client_fd)
{
    rio_t rio;
    Rio_readinitb(&rio,server_fd);
    int n;
    int block_size=0;
    char buf[MAXLINE];
    //cache 的缓冲区
    char* cache_buf=Malloc(MAX_OBJECT_SIZE);
    char* ptr=cache_buf;
    
    //该url的cache列表
    cache_head *content_header=init_cache_head_node();
    cache_t *content_block=init_cache_node();

    while((n=Rio_readnb(&rio,buf,MAXLINE)))
    {
        Rio_writen(client_fd, buf, n);

        if(block_size+n>=MAX_OBJECT_SIZE)
        {
            //够了一个最大的object,先将该块插入
            char *content=Malloc(block_size);
            strncpy(content,cache_buf,block_size);
            content_block->content=content;
            content_block->size=block_size;
            insert_node_to_head(content_header,content_block);
            //重新记录内容
            ptr=cache_buf;
            //创建新的信息节点
            content_block=init_cache_node();
            block_size=0;            
        }
        strncpy(ptr,buf,n);
        ptr+=n;
        block_size+=n;
    }

    //插入最后一个信息块
    char *content=Malloc(block_size);
    strncpy(content,cache_buf,block_size);
    content_block->content=content;
    content_block->size=block_size;
    insert_node_to_head(content_header,content_block);

    //无法缓存
    if(content_header->total_size>MAX_CACHE_SIZE)
        free_header(content_header);

    //将信息头节点插入cache链表    
    else
        insert_head_to_list(content_header);
    Free(cache_buf);
}

void insert_node_to_head(cache_head* head,cache_t* cache_node)
{
    //空头节点
    if(head->last==NULL)
    {
        head->total_size+=cache_node->size;
        head->last=cache_node;
        head->content=cache_node;
        return;
    }
    head->total_size+=cache_node->size;
    head->last->next_content=cache_node;
    head->last=cache_node;
}

void insert_head_to_list(cache_head* head)
{
    if(remain_cache_size>head->total_size)
    {
        head->next=cache_list_head->next;
        cache_list_head->next=head; 
        remain_cache_size-=head->total_size;
        return;
    }
    size_t need=head->total_size;
    while(need>0)
    {
        cache_head *cur=cache_list_head->next;
        cache_head *place=NULL;
        unsigned int last_use=(1<<30)-1;
        while(cur)
        {
            if(cur->time_stamp<last_use)
            {
                last_use=cur->time_stamp;
                place=cur;
            }
        }
        need-=place->total_size;
        free_header(place);
    }
    remain_cache_size-=head->total_size;
}

void free_header(cache_head* head)
{
    cache_head *cur=cache_list_head->next;
    while(cur)
    {
        if(cur->next==head)
        {
            cur->next=head->next;
            break;
        }
    }
    cache_t *content_cur=head->content;
    while(content_cur)
    {
        cache_t *tmp=content_cur;
        Free(content_cur);
        content_cur=tmp->next_content;
    }
    Free(head);
}

void rwlock_init(rwlock_t *rw) { 
    rw->readers = 0; 
    sem_init(&rw->lock, 0, 1); 
    sem_init(&rw->writelock, 0, 1); 
} 
 
void rwlock_acquire_readlock(rwlock_t *rw) { 
    P(&rw->lock); 
    rw->readers++; 
    if(rw->readers == 1) 
        P(&rw->writelock); // first reader acquires writelock 
    V(&rw->lock); 
 } 
 
void rwlock_release_readlock(rwlock_t *rw) { 
    P(&rw->lock); 
    rw->readers--; 
    if(rw->readers == 0) 
        V(&rw->writelock); // last reader releases writelock 
    V(&rw->lock); 
 } 
 
void rwlock_acquire_writelock(rwlock_t *rw) { 
    P(&rw->writelock); 
 } 
 
void rwlock_release_writelock(rwlock_t *rw) { 
    V(&rw->writelock); 
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
    sprintf(buf, "<html><title>Proxy Error</title>");
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