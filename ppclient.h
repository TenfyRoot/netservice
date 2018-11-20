#ifndef MIDSERVER_H
#define MIDSERVER_H
#include "global.h"
#include <stdarg.h>
#include <assert.h>

namespace ppclient {

struct tagParam {
	void* param;
};

class ppclient {
public:
	ppclient();
	~ppclient();
	
	int  startconnect(const char* ip);
	void ProcListenHole(void* param);
	void ProcMakeHole(void* param);
	void mainconnectrecv(int sockfd, const char* data, int size);
	void newuserholerecv(int sockfd, const char* data, int size);
	void connectrecv(int sockfd, const char* data, int size);
	void listenrecv(int sockfd, const char* data, int size);

private:
	void HandleNewUserLogin(int sockfd, tagNewUserLoginPkt* pNewUserLoginPkt);
	void HandleSrvReqMakeHole(int sockfd, tagSrvReqMakeHolePkt* pSrvReqMakeHolePkt);
	
private:
	tagWelcomePkt myWelcomePkt;
	char serverip[32];
};

extern ppclient* instance;
void inst(int create = 1);

typedef void (*logcallback)(int level, const char*, va_list);
extern logcallback logfun;
}
#endif
