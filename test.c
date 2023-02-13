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
    char request[MAXLINE],header_host[MAXLINE];
    memset(request,0,MAXLINE);
    memset(header_host,0,MAXLINE);
    sprintf(request,"GET %s HTTP/1.0\r\n","/jjj/uuu");
    sprintf(header_host,"Host: %s\r\n","hqz.com");
    sprintf(request,"%s%s",request,header_host);
    sprintf(request,"%s%s",request,user_agent_hdr);
    sprintf(request,"%s%s",request,connection_hdr);
    sprintf(request,"%s%s",request,Proxy_connection_hdr);
    sprintf(request,"%s\r\n",request);
    puts(request);
    return 0;
}