#include <stdio.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define Err int
#define ERR 1
#define NIL 0

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

void doit(int fd);
Err parse_uri(char *uri,UriData *uri_data);
Err read_requesthdr(rio_t *rp,UriData *uri_data);
void send_request(int server_fd,int client_fd,UriData *uri_data);
void send_response(int server_fd,int client_fd);

int main(int argc,char **argv)
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1) {
	clientlen = sizeof(clientaddr);
	connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
	doit(connfd);                                             //line:netp:tiny:doit
	Close(connfd);                                            //line:netp:tiny:close
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
    if(!strcmp(method,"GET"))
        return;
    
    //解析请求内容
    err=parse_uri(uri,&uri_data);
    if(err)return;

    //如果hostname未知，通过请求头查询
    if(!strcmp(uri_data.host_name,"UNKNOW"))
        err=read_requesthdr(&rio,&uri_data);
    if(err)return;

    //解析完可以建立连接了
    int server_fd;
    server_fd=Open_clientfd(uri_data.host_name,uri_data.port);

    send_request(server_fd,client_fd,&uri_data);

    send_response(server_fd,client_fd);

    Close(server_fd);
    Close(client_fd);

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
    return NIL;
    
}

//读取请求头
Err read_requesthdr(rio_t *rp,UriData *uri_data)
{
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

void send_request(int server_fd,int client_fd,UriData *uri_data)
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

void send_response(int server_fd,int client_fd)
{
    rio_t rio;
    Rio_readinitb(&rio,server_fd);
    int n;
    char buf[MAXLINE];
    while(n=Rio_readlineb(&rio,buf,MAXLINE))
    {
        Rio_writen(client_fd, buf, n);
    }
}