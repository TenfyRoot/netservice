#ifndef NETSERVICE_H
#define NETSERVICE_H

#include <pthread.h>
#include <stdarg.h>
#include <netinet/in.h>
#include <string.h>
#include <map>
#include <vector>

namespace netservice {

#define MAXTHREADNUM 20

typedef struct  {
	void* param;
} tagParam;

typedef struct _tagConfig : tagParam {
	int sockto;
	int sockfrom;
	char domain[128];
	char ipfrom[16];
	int portfrom;
	char ipto[16];
	int portto;
	_tagConfig(){
		portto = 80;
		sockfrom = -1;
		sockto = -1;
		strcpy(ipto,"127.0.0.1");
	}
	_tagConfig(const char* ip, int port){
		sockfrom = -1;
		sockto = -1;
		strcpy(ipto,ip);
		portto = port;
	}
	_tagConfig(const char* ip){
		sockfrom = -1;
		sockto = -1;
		strcpy(ipto,ip);
		portto = 80;
	}
	_tagConfig(int port){
		sockfrom = -1;
		sockto = -1;
		strcpy(ipto,"127.0.0.1");
		portto = port;
	}
} tagConfig;

typedef struct _tagStartServerParam : tagParam {
	int sockfd;
	int cpt;
} tagStartServerParam;

typedef struct _tagIndex : tagParam {
	int i;
} tagIndex;

typedef struct _tagVecConfig : tagParam {
	std::vector<tagConfig>* pVecConfig;
} tagVecConfig;

#define MAX_LINE 16384

//#define  FD_SETSIZE 1024
struct fd_state 
{
    char buffer[MAX_LINE];
    size_t buffer_used;

    int writing;
    size_t n_written;
    size_t write_upto;
};

class tcpservice {
public:
	tcpservice();
	~tcpservice();

	void startserver(int port, int listencount = 100, int recvthreadcount = 1);
	void procstartserver(void *param);
	void startrecv(int sockfd);
	void procrecv(void* param);
	void startservertrans(const char* svrip, int svrport, std::vector<tagConfig>& vecConfig);
	void procfromto(void *param);
	void proctrans(void *param);
	int  connecthost(const char* ip, int port,int reuseaddr);
	int  connecthost(unsigned long dwip, int port,int reuseaddr);
	int  datasend(int sockfd, const char* buf, int bufsize);
	
private:
	void reset();
	void stop();
	struct fd_state * alloc_fd_state(void);
	int  doread(int fd, struct fd_state *state);
	int  dowrite(int fd, struct fd_state *state);
	
private:
	pthread_t mpthreadstartserver;
	bool bstartserverworking;
	
	pthread_t mpthreadfromto;
	bool bfromtoworking;
	
	bool btransworking;
	int maxsock;
	
	pthread_t mpthreadrecv[MAXTHREADNUM];
	bool brecvworking[MAXTHREADNUM];
	std::map<int,struct sockaddr_in> mapsock[MAXTHREADNUM];
	
	struct fd_state *fdstate[FD_SETSIZE];
};

extern tcpservice* tcp;
void inst(int create = 1);

typedef void (*logcallback)(int level, const char*, va_list);
extern logcallback logfun;
}

#endif
