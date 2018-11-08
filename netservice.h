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
		sockfrom = -1;
		sockto = -1;
		strcpy(ipto,"127.0.0.1");
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

class tcpservice {
public:
	tcpservice();
	~tcpservice();

	void startserver(int port, int listencount = 100, int recvthreadcount = 1);
	void procstartserver(void *param);
	void startrecv(int sockfd);
	void procrecv(void* param);
	void startmakehole(const char* svrip, int svrport, std::vector<tagConfig>& vecConfig);
	void procfromto(void *param);
	void proctrans(void *param);
	int  connecthost(const char* ip, int port,int reuseaddr);
	int  connecthost(unsigned long dwip, int port,int reuseaddr);
	int  datasend(int sockfd, const char* buf, int bufsize);
	
private:
	void reset();
	void stop();
	
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
};

extern tcpservice* tcp;
void inst(int create = 1);

typedef void (*logcallback)(int level, const char*, va_list);
extern logcallback logfun;
}

#endif
