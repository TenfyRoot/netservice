#ifndef NETSERVICE_H
#define NETSERVICE_H

#include <pthread.h>
#include <stdarg.h>
#include <netinet/in.h>
#include <string.h>
#include <map>
#include <vector>
#include <sys/epoll.h>

namespace netservice {

#define MAXTHREADNUM 20

typedef std::vector<epoll_event> VectorEpEvent;

typedef struct  {
	void* param;
} tagParam;

typedef struct _tagConfig : tagParam {
	int sockrecv;
	int sockhole;
	int sockto;
	char domain[128];
	char ipfrom[16];
	int portfrom;
	char ipto[16];
	int portto;
	_tagConfig(){
		sockrecv = -1;
		sockhole = -1;
		sockto = -1;
		strcpy(ipto,"127.0.0.1");
		portto = 80;
	}
	_tagConfig(const char* ip, int port){
		sockrecv = -1;
		sockhole = -1;
		sockto = -1;
		strcpy(ipto,ip);
		portto = port;
	}
	_tagConfig(const char* ip){
		sockrecv = -1;
		sockhole = -1;
		sockto = -1;
		strcpy(ipto,ip);
		portto = 80;
	}
	_tagConfig(int port){
		sockrecv = -1;
		sockhole = -1;
		sockto = -1;
		strcpy(ipto,"127.0.0.1");
		portto = port;
	}
} tagConfig;

typedef struct _tagStartServerParam : tagParam {
	int cpt;
} tagStartServerParam;

typedef struct _tagIndex : tagParam {
	int i;
} tagIndex;

typedef struct _tagVecConfig : tagParam {
	std::vector<tagConfig>* pVecConfig;
} tagVecConfig;

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
	void addfd(int& epfd, int opfd);
	void delfd(int& epfd, int opfd);
	
private:
	pthread_t mpthreadstartserver;
	bool bstartserverworking;
	
	pthread_t mpthreadfromto;
	bool bfromtoworking;
	bool btransworking;
	
	pthread_t mpthreadrecv[MAXTHREADNUM];
	bool mbrecvworking[MAXTHREADNUM];
	
	int mListenEpollfd, mRecvEpollfd[MAXTHREADNUM], mHoleEpollfd, mTransEpollfd;
	VectorEpEvent mvecListenEpEvent, mvecRecvEpEvent[MAXTHREADNUM], mvecHoleEpEvent, mvecTransEpEvent;
	std::map<int, std::vector<int> > mmapEpfd;
	std::map<int,int> mmapfdflag;
};

extern tcpservice* tcp;
void inst(int create = 1);

typedef void (*logcallback)(int level, const char*, va_list);
extern logcallback logfun;
}

#endif
