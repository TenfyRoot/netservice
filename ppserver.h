#ifndef PPSERVER_H
#define PPSERVER_H
#include "global.h"
#include <map>
#include <stdarg.h>
#include <semaphore.h>

namespace ppserver {

struct tagParam {
	void* param;
};

class ppserver {
public:
	ppserver();
	~ppserver();
	
	void startserver();
	void ProcDisConnect(void* param);
	void mainserveraccpet(int socklisten, int sockaccept);
	void holeserveraccpet(int socklisten, int sockaccept);
	void mainserverrecv(int sockfd, const char* data, int size);
	void holeserverrecv(int sockfd, const char* data, int size);

private:
	tagWelcomePkt myWelcomePkt;
	std::map<int, tagNewUserLoginPkt> mapNewUserLoginPkt;
	std::map<int, tagNewUserLoginPkt> mapNewUserLoginPktMain;
	std::map<int, sem_t> mapSem;
	
	
};

extern ppserver* instance;
void inst(int create = 1);

typedef void (*logcallback)(int level, const char*, va_list);
extern logcallback logfun;
}
#endif
