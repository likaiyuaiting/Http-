#include<stdio.h>
#include<unistd.h>
#include<pthread.h>
#include<string.h>
#include<stdlib.h>
#include<sys/stat.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/sendfile.h>//sendfile函数的头文件
#include<fcntl.h>

typedef struct sockaddr sockaddr;
typedef struct sockaddr_in sockaddr_in;

#define SIZE 10240

typedef struct Request{
	char first_line[SIZE];
	char* method;
	char* url;
	char* url_path;   //重点关注的内容1
	char* query_string;//重点关注的内容2
	//char* version;
	//接下来是 header 部分，如果要完整的解析下来
	//此处需要使用二叉搜索树或者 hash 表
 	//这里我们偷个懒，其他header都不要了，只保留一个Content-Length
	int content_length;
}Request;
//一次从socket中读取一行数据
//把数据放到buf缓冲区中
//如果读取失败，返回-1
//换行符: \n. \r.   \r\b
int ReadLine(int sock,char buf[],ssize_t size){
	//1，从socket中一次读取一个字符
	char c='\0';
	ssize_t i=0; //当前读取了多少个字符
	//结束条件：
	//a)读的长度太长，达到了缓冲区上限。
	//b)读到了\n（）
	//如果读取到\n就返回
	while((i<size-1)&&(c=='\n'))//如果读取到\n就返回
	{
		ssize_t read_size=recv(sock,&c,1,0);
		if(read_size<0){
			return -1;
		}
		if(c=='\r')
		{
			//当前遇到了\r，但是还需要确定下一个字符是不是\n
			//MSG_PEEK选项从内核的缓冲区中读取数据，但是读取的数据不会从缓冲区中删除掉
			recv(sock,&c,1,MSG_PEEK);
			if(c=='\n')
			{
				//当前分隔符是\r\n
				recv(sock,&c,1,0);
			}else{
				//当前分隔符确定是\r，此时把分割符转换成\n.
				c='\n';
			}
		}
		//统一了换行符都以\n换行
		buf[i++]=c;
	}
	buf[i]='\0';
	return i;//真正缓冲区中放置的字符个数
}

int Split(char input[],const char* split_char,char* output[],int output_size)
{
	//使用strtok
	/*
	strtok函数由于内部维持了一个静态的变量来保存切分的位置，故而它线程不安全
	我们使用strtok_r,来进行字符串的切分
	*/
	int i=0;
	char* pch;
	char* tmp=NULL;
	//pch=strtok(input,split_char);
	pch=strtok_r(input,split_char,&tmp);
	while(pch!=NULL)
	{
		if(i>=output_size)
			return i;
		output[i++]=pch;
		//pch=strtok(NULL,split_char);
		pch=strtok_r(NULL,split_char,&tmp);
	}
	return i;
}

int ParseFirstLine(char first_line[],char** p_url,char** p_method)
{
	//首行按照空格进行字符串切分
	char* tok[10];
	//把 first_line按照空格进行切分
	//切分的吗，每个部分，就放到tok数组中。
	//返回值，就是tok 数组中包含的几个元素
	//最后一个参数10表示tok最多能放几个元素
	int tok_size=Split(first_line," ",tok,10);
	if(tok_size!=3)
	{
		printf("Split failed! tok_size=%d\n",tok_size);
		return -1;
	}
	*p_method=tok[0];
	*p_url=tok[1];
	return 0;
}

int ParseQueryString(char* url,char** p_url_path,char** p_quetry_string)
{
	char* p=url;
	*p_url_path=url;
	for(;*p!='\0';++p)
	{
		if(*p=='?')
		{
			*p='\0';
			*p_quetry_string=p+1;
			return 0;
		}
	}
	//循环结束都没有找到？，说明这个请求不带query_string
	*p_quetry_string=NULL;
	return 0;
}

int ParseHeader(int sock,int *content_length)
{
	char buf[SIZE]={0};
	while(1){
	//1,循环从socket中读取一行
	ssize_t read_size=ReadLine(sock,buf,sizeof(buf));
	//处理读失败的情况
	if(read_size<=0)
		return -1;
	//5.读到空行，循环结束
	if(strcmp(buf,"\n")==0)
		return 0;
	
	//2,判定当前行是不是Content-Length
	//   如果是就直接把value读取出来
	//   如果不是就直接扔掉
	const char* content_length_str="Content-Length: ";
	if(content_length!=NULL&&strncmp(buf,content_length_str,strlen(content_length_str))==0)
		*content_length=atoi(buf+strlen(content_length_str));
	}
	return 0;
}

void Handler404(int sock)
{
	//构造一个完整的HTTP响应
	//状态码为404
	//body部分应该也是一个404相关的错误页面
	const char*first_line="HTTP/1.1 404 Not Found";
	const char* type_line="Content-Type: text/html:charset=utf-8\n";
	const char* blank_line="\n";
	const char*html="<head><meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\"></head>"
	"<h1>你的页面走没了</h1>";
	send(sock,first_line,strlen(first_line),0);
	send(sock,type_line,strlen(type_line),0);
	send(sock,blank_line,strlen(blank_line),0);
	send(sock,html,strlen(html),0);
	return ;
}

void PrintRequest(Request* req)
{
	printf("method:%s\n",req->method);
	printf("url_path:%s\n",req->url_path);
	printf("query_string:%s\n",req->query_string);
	printf("content_length:%d\n",req->content_length);
}


int HandlerCGI()
{
	return 404;
}

int IsDir(const char* file_path)
{
	struct stat st;
	int ret=stat(file_path,&st);
	if(ret<0)
		return 0;
	if(S_ISDIR(st.st_mode)){
		return 1;
	}
	return 0;
}
void HandlerFilePath(const char* url_path,char file_path[]){
	//a)给url_path加上前缀（HTTP服务器的根目录）
	//url_path==>/index.html
	//file_path=>./wwwroot/index.html
	sprintf(file_path,"./wwwroot%s",url_path);
	//b)例如url_path==>/. 此时url_path其实是一个目录
	//如果是一个目录的话，就给这个目录后面加上一个index.html
	//url_path /或者 /image/
	if(file_path[strlen(file_path)-1]=='/'){
		strcat(file_path,"index.html");
	}
	//c) 例如url_path => /image
	if(IsDir(file_path)){
		strcat(file_path,"/index.html");
	}
	return;
}

ssize_t GetFileSize(const char* file_path)
{
	struct stat st;
	int ret=stat(file_path,&st);
	if(ret<0){
		//打开文件失败，很可能是文件不存在
		//此时返回文件长度问0
		return 0;
	}
	return st.st_size;
} 
int WriteStaticFile(int sock,const char* file_path){
	//1.打开文件
	//什么情况下会打开失败？
	int fd=open(file_path,O_RDONLY);
	if(fd<0)
	{
		perror("open");
		return 404;
	}
	//2.把构造的HTTP响应写入到socket中
	//  a)写入首行
	const char* first_line="HTTP/1.1 200 OK\n";
	send(sock,first_line,strlen(first_line),0);
	//  b)写入header
	/*我们想在浏览器中将我们的东西展示出来，但是会有格式的差别（文本，图片，等）
	//我们处理是有二中方案。
	//方案一：解析Url中的后缀名，来达到正确输出的效果
	const char* type_line="Content-Type: text/html:charset=utf-8\n";//文本解析方式
	//const char* type_line="Content-Type: image/jpg:charset=utf-8\n";//图片解析方式
	send(sock,type_line,strlen(type_line),0);
	//方案二：不直接指定解析方式，有浏览器进行自动识别。（优）
	*/
	//  c)写入空行
	const char* blank_line="\n";
	send(sock,blank_line,strlen(blank_line),0);
	
	//d)写入body（文件内容）
	/*
	ssixe_t file_size=GetFileSize(file_path);
	int i=0;
	for(;i<file_size;++i)
	{
		char c;
		read(fd,&c,1);
		send(sock,&c,1,0);
	}
	*/
	sendfile(sock ,fd,NULL,GetFileSize(file_path));//（优）
	//3.关闭文件
	close(fd);
	return 200;
}

int HandlerStartFile(int sock,Request* req)
{
	//1.根据url_path获取到文件在服务器上的真实路径
	char file_path[SIZE]={0};
	HandlerFilePath(req->url_path,file_path);
	//2.读取文件，把文件的内容直接写到socket中
	int err_code=WriteStaticFile(sock,file_path);
	return err_code;
}

void HandlerRequest(int new_sock){
	int err_code=200;
	//1，读取并解析请求（反序列化）
	Request req;
	memset(&req,0,sizeof(req));
	//a)从socket中读取出首行
	if(ReadLine(new_sock,req.first_line,sizeof(req.first_line))<0){
		//错误处理
		err_code=404;
		goto END;
	}
	//b)解析首行，从首行中解析出url和method
	if(ParseFirstLine(req.first_line,&req.url,&req.method)<0){
		err_code=404;
		goto END;
	}
	//c)解析url，从url中解析出url_path
	if(ParseQueryString(req.url,&req.url_path,&req.query_string)<0){
		err_code=404;
		goto END;
	}
	//d)处理Header，只读取Conten-Length
	if(ParseHeader(new_sock,&req.content_length)){
		err_code=404;
		goto END;
	}
	
	PrintRequest(&req);//打印信息
	
	//2，静态/动态方式生成页面,把生成的结果写回到客户端上
	//if(strcmp(req.method,"GET")==0&&req.content_length==NULL)
		//考虑到浏览器返回的可能是"get“，”Get“,故而我们使用strcasecmp函数代替strcmp函数。
	if(strcasecmp(req.method,"GET")==0&&req.content_length==0)
	{
		//   a)如果请求是GET，并且没有query_string,返回静态页面
		err_code=HandlerStartFile(new_sock,&req);
	}else if(strcasecmp(req.method,"GET")==0&&req.content_length!=0){
	//   b)如果请求是GET，并且有query_string,返回动态页面
		err_code=HandlerCGI();
	}else if(strcmp(req.method,"POST")==0){
	//   c)如果请求是POSt（一定带有参数的，参数在body部分）返回动态页面
		err_code=HandlerCGI();
	}else{
		//错误处理
		err_code=404;
		goto END;
	}
END:
	if(err_code==404){
		Handler404(new_sock);
		close(new_sock);
	}
}
void* ThreadEntry(void* arg){
		int64_t new_sock=(int64_t)arg;
		//此处使用HandlerRequest函数进行完成具体的处理请求过程。
		//这个过程单独提取出来也是为了解耦合。
		//一旦需要把服务器改成多进程或多路IO多路复用的形式。
		//整体的代码改动都是比较小的
		HandlerRequest(new_sock);
		return NULL;
}

void HttpServerStart(const char* ip,short port)
{
	int listen_sock=socket(AF_INET,SOCK_STREAM,0);
	if(listen_sock<0)
	{
		perror("sock");
		return ;
	}
	
	//端口复用（加上这个就能重用TIME_WAIT链接）
	int opt=1;
	setsockopt(listen_sock,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
	
	sockaddr_in addr;
	addr.sin_family=AF_INET;
	addr.sin_addr.s_addr=inet_addr(ip);
	addr.sin_port=htons(port);
	int ret=bind(listen_sock,(sockaddr*)&addr,sizeof(addr));
	if(ret<0){
		perror("bind");
		return;
	}
	ret=listen(listen_sock,5);
	if(ret<0){
		perror("listen");
		return;
	}
	
	printf("ServerInit ok\n");
	
	while(1)
	{
		sockaddr_in peer;
		socklen_t len=sizeof(peer);
		int64_t new_sock=accept(listen_sock,(sockaddr*)&peer,&len);
		if(new_sock<0){
			perror("accept");
			continue;
		}
		//使用多线程的方式来实现TCP服务器
		pthread_t tid;
		pthread_create(&tid,NULL,ThreadEntry,(void*)new_sock);
		pthread_detach(tid);
	}
}


//./http_server [ip] [port]
int main(int argc,char* argv[])
{
	if(argc!=3){
		printf("Usage .http_server [ip] [port]\n");
		return 1;
	}
	HttpServerStart(argv[1],atoi(argv[2]));
	return 0;
}
