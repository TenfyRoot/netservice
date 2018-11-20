#ifndef NETSERVICE_H
#define NETSERVICE_H

#include <pthread.h>
#include <stdarg.h>
#include <netinet/in.h>
#include <string.h>
#include <map>
#include <vector>
#include <sys/epoll.h>
#include <pthread.h>

namespace netservice {

#define MAXTHREADNUM 100

class Mutex;

struct tagParam {
	void* param;
};

struct tagConfig : tagParam {
	int  sockhole;
	char domain[128];
	char ipfrom[16];
	int  portfrom;
	char ipto[16];
	int  portto;
	tagConfig() {
		sockhole = -1;
		strcpy(ipto,"127.0.0.1");
		portto = 80;
	}
	tagConfig(const char* ip, int port) {
		sockhole = -1;
		strcpy(ipto,ip);
		portto = port;
	}
	tagConfig(const char* ip) {
		sockhole = -1;
		strcpy(ipto,ip);
		portto = 80;
	}
	tagConfig(int port) {
		sockhole = -1;
		strcpy(ipto,"127.0.0.1");
		portto = port;
	}
};

struct tagStartServerParam : tagParam {
	int cpt;
	int port;
};

struct tagIndex : tagParam {
	int i;
	void *data;
	tagIndex():i(0),data(0) {}
};

struct tagVecConfig : tagParam {
	std::vector<tagConfig>* pVecConfig;
};

struct tagTransParam : tagParam {
	bool btransworking;
	pthread_t pthreadtrans;
	int sockrecv;
	int sockto;
	tagTransParam() {
		btransworking = false;
		pthreadtrans = 0;
		sockrecv = -1;
		sockto = -1;
	}
	tagTransParam(int sockfd1, int sockfd2) {
		btransworking = false;
		pthreadtrans = 0;
		sockrecv = sockfd1;
		sockto = sockfd2;
	}
};

class Mutex
{
    friend class CondVar;
    pthread_mutex_t  m_mutex;

  public:
    Mutex() { pthread_mutex_init(&m_mutex, NULL); }
    virtual ~Mutex() {
	pthread_mutex_unlock(&m_mutex);
	pthread_mutex_destroy(&m_mutex);
    }

    int lock() { return  pthread_mutex_lock(&m_mutex); }
    int trylock() { return  pthread_mutex_trylock(&m_mutex); }
    int unlock() { return  pthread_mutex_unlock(&m_mutex); }   
};

typedef void (*callbackaccept)(void* param, int socklisten, int sockaccpet);
typedef void (*callbackrecv)(void* param, int sockfd, const char* pch, int size);
struct tagCallAccept {
	void* param;
	callbackaccept cb;
	tagCallAccept() : param(0), cb(0) {}
	tagCallAccept(void* ptr, callbackaccept cbfunc)
		: param(ptr), cb(cbfunc) {}
};
struct tagCallRecv {
	void* param;
	callbackrecv cb;
	tagCallRecv() : param(0), cb(0) {}
	tagCallRecv(void* ptr, callbackrecv cbfunc)
		: param(ptr), cb(cbfunc) {}
};
class tcpservice {
public:
	tcpservice();
	~tcpservice();

	void startserver(int port, callbackrecv cbrecv = 0, void* paramrecv = 0, \
		callbackaccept cbaccept = 0, void* paramaccept = 0, \
		int recvthreadcount = MAXTHREADNUM, int listencount = MAXTHREADNUM);
	void procstartserver(void *param);
	int  startconnect(const char* ip, int port, callbackrecv callback = 0, void* paramrecv = 0, int bindport = 0);
	void stopconnect(int sockfd);
	void procrecv(void* param);
	void startservertrans(const char* svrip, int svrport, std::vector<tagConfig>& vecConfig);
	void procfromto(void *param);
	void proctrans(void *param);
	bool datasend(int sockfd, const char* buf, int bufsize);

private:
	void reset();
	void stop();
	int  connecthost(const char* ip, int port, int reuseaddr);
	int  connecthost(const char* ip, int port, int bindport, int reuseaddr);
	int  connecthost(unsigned long dwip, int port, int bindport, int reuseaddr);
	void setsockbuf(int sockfd);
	void setnonblock(int sockfd);
	void addfd(int& epfd, int opfd);
	void delfd(int& epfd, int opfd);
	void clrfd(int& epfd);
	
private:
	pthread_t mpthreadstartserver;
	bool bstartserverworking;
	
	pthread_t mpthreadfromto;
	bool bfromtoworking;
	bool btransworking;
	
	pthread_t mpthreadrecv[MAXTHREADNUM];
	bool mbrecvworking[MAXTHREADNUM];
	
	int mListenEpollfd, mRecvEpollfd[MAXTHREADNUM], mHoleEpollfd;
	std::map<int, std::vector<int> > mmapEpfd;
	std::map<int, tagTransParam> mmapTransParam;
	Mutex mmutex;
	Mutex mmutexep;
	
	std::map<int, std::vector<int> > mmapListenfdClientfds;
	std::map<int, tagCallRecv> mmapRecvFunc;
	std::map<int, tagCallAccept> mmapAcceptFunc;
};

extern tcpservice* instance;
void inst(int create = 1);

typedef void (*logcallback)(int level, const char*, va_list);
extern logcallback logfun;
}

#endif
