#include<stdio.h>
#include<string.h>
#define Err int
#define ERR 1
#define NIL 0
#define MAXLINE 100
typedef struct uriData
{
    char host_name[MAXLINE];
    char port[MAXLINE];
    char path[MAXLINE];
}UriData;

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr="Connection: close\r\n";
static const char *Proxy_connection_hdr="Proxy-Connection: close\r\n";

int main()
{
    char *buf="GET http://localhost:8080/home.html HTTP/1.1\r\n";
    char uri[MAXLINE],method[MAXLINE],version[MAXLINE];
    printf("request header from client:\n");
    printf("%s",buf);
    sscanf(buf,"%s %s %s",method,uri,version);

    //只接受GET
    puts(method);
    printf("%d\n",strlen(method));
    printf("%d\n",strcmp(method,"GET"));
    return 0;
}