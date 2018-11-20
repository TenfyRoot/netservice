#include "ppserver.h"
#include <arpa/inet.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include "netservice.h"

namespace ppserver {

#define release(p) {if (p) {delete p; p = 0;}}

void log(const char* format, ...);
void log(int level, const char* format, ...);
void loglevel(int level, const char* format, va_list valst);

struct tagDirectConnect : tagParam{
	int sockfd;
	tagSrvReqDirectConnectPkt SrvReqDirectConnectPkt;
};

ppserver* instance = 0;
void inst(int create) {
	if (!create) release(instance);
	if (create && !instance) {instance = new ppserver();}	
}

void* ThreadProcDisConnect(void* param) {
	((ppserver*)((tagParam*)param)->param)->ProcDisConnect(param); return 0;
}

void call_mainserveraccpet(void* param, int socklisten, int sockaccept) {
	((ppserver*)(param))->mainserveraccpet(socklisten, sockaccept);
}
void call_holeserveraccpet(void* param, int socklisten, int sockaccept) {
	((ppserver*)(param))->holeserveraccpet(socklisten, sockaccept);
}
void call_mainserverrecv(void* param, int sockfd, const char* data, int size) {
	((ppserver*)(param))->mainserverrecv(sockfd, data, size);
}
void call_holeserverrecv(void* param, int sockfd, const char* data, int size) {
	((ppserver*)(param))->holeserverrecv(sockfd, data, size);
}

ppserver::ppserver()
{
	log ("tagWelcomePkt:%d, tagNewUserLoginPkt:%d, tagSrvReqMakeHolePkt:%d, tagReqConnClientPkt:%d, tagHoleListenReadyPkt:%d, tagReqSrvDisconnectPkt:%d",
		sizeof(tagWelcomePkt),
		sizeof(tagNewUserLoginPkt),
		sizeof(tagSrvReqMakeHolePkt),
		sizeof(tagReqConnClientPkt),
		sizeof(tagHoleListenReadyPkt),
		sizeof(tagReqSrvDisconnectPkt)
		);
}

ppserver::~ppserver()
{
	
}

void ppserver::startserver()
{
	netservice::instance->startserver(SRVTCPMAINPORT, call_mainserverrecv, this, call_mainserveraccpet, this);
	netservice::instance->startserver(SRVTCPHOLEPORT, call_holeserverrecv, this, call_holeserveraccpet, this);
}

void ppserver::mainserveraccpet(int socklisten, int sockaccept)
{
	struct sockaddr_in addr;
	socklen_t socklen = sizeof(struct sockaddr);
	bzero(&addr, sizeof(struct sockaddr_in)); getpeername(sockaccept, (struct sockaddr *)(&addr), &socklen);
	
	tagNewUserLoginPkt NewUserLoginPkt;
	inet_ntop(AF_INET, &addr.sin_addr, NewUserLoginPkt.szClientIP, sizeof(NewUserLoginPkt.szClientIP));
	NewUserLoginPkt.nClientPort = ntohs(addr.sin_port);
	NewUserLoginPkt.dwID = sockaccept;
	log("%s[%d] [main] client enter %s:%d:%d", __FUNCTION__,__LINE__, NewUserLoginPkt.szClientIP, NewUserLoginPkt.nClientPort, NewUserLoginPkt.dwID);
	
	tagWelcomePkt WelcomePkt;
	strcpy ( WelcomePkt.szClientIP, NewUserLoginPkt.szClientIP );
	WelcomePkt.nClientPort = NewUserLoginPkt.nClientPort;
	WelcomePkt.dwID = NewUserLoginPkt.dwID;
	sprintf ( WelcomePkt.szWelcomeInfo, "Hello, ID.%d, Welcome to login", WelcomePkt.dwID );
	netservice::instance->datasend( sockaccept, (char*)&WelcomePkt, (int)sizeof(tagWelcomePkt) );
	
	std::map<int, tagNewUserLoginPkt>::iterator itermap;
	for (itermap = mapNewUserLoginPktMain.begin(); itermap != mapNewUserLoginPktMain.end(); ++itermap) {
		netservice::instance->datasend( itermap->first, (char*)&NewUserLoginPkt, (int)sizeof(tagNewUserLoginPkt) );
	}
	mapNewUserLoginPkt[NewUserLoginPkt.dwID] = NewUserLoginPkt;
	
	mapNewUserLoginPktMain.clear();
	mapNewUserLoginPktMain[NewUserLoginPkt.dwID] = NewUserLoginPkt;
}

void ppserver::holeserveraccpet(int socklisten, int sockaccept)
{
	struct sockaddr_in addr;
	socklen_t socklen = sizeof(struct sockaddr);
	bzero(&addr, sizeof(struct sockaddr_in)); getpeername(sockaccept, (struct sockaddr *)(&addr), &socklen);
	
	tagNewUserLoginPkt NewUserLoginPkt;
	inet_ntop(AF_INET, &addr.sin_addr, NewUserLoginPkt.szClientIP, sizeof(NewUserLoginPkt.szClientIP));
	NewUserLoginPkt.nClientPort = ntohs(addr.sin_port);
	NewUserLoginPkt.dwID = sockaccept;
	mapNewUserLoginPkt[NewUserLoginPkt.dwID] = NewUserLoginPkt;
	
	log("%s[%d] [hole] client enter %s:%d:%d", __FUNCTION__,__LINE__, NewUserLoginPkt.szClientIP, NewUserLoginPkt.nClientPort, NewUserLoginPkt.dwID);	
}

void ppserver::mainserverrecv(int sockfd, const char* data, int size)
{
	if ( !data || size < 4 ) return;
	bool bmainserverrecv = true;
	
	PACKET_TYPE *pePacketType = (PACKET_TYPE*)data;
	assert ( pePacketType );
	switch ( *pePacketType ) {
	case PACKET_TYPE_HOLE_LISTEN_READY:
		{// 被动端（客户端B）打洞和侦听均已准备就绪
			log ("[main] tagHoleListenReadyPkt:%d size:%d", sizeof(tagHoleListenReadyPkt), size);
			assert ( size == sizeof(tagHoleListenReadyPkt) );
			tagHoleListenReadyPkt *pHoleListenReadyPkt = (tagHoleListenReadyPkt*)data;
			assert ( pHoleListenReadyPkt );
			log ( "[main-%d] Client.%d hole and listen ready", sockfd, pHoleListenReadyPkt->dwInvitedID );
			
			// 通知正在与客户端A通信的服务器线程，以将客户端B的外部IP地址和端口号告诉客户端A
			if (mapSem.find(sockfd) != mapSem.end()) {
				sem_post(&mapSem[sockfd]);
			} else {
				log ( "[main-%d] not found sem", sockfd );
			}
			break;
		}
	default:
		{
			log ("[main-%d] size:%d", sockfd, size);
			assert(!bmainserverrecv);
			break;
		}
	}
}

void ppserver::holeserverrecv(int sockfd, const char* data, int size)
{
	if ( !data || size < 4 ) return;
	bool bholeserverrecv = true;
	
	if (mapNewUserLoginPkt.find(sockfd) == mapNewUserLoginPkt.end()) {
		log ( "[hole-%d] not found inviterHoleID", sockfd);
		return;
	}
	tagNewUserLoginPkt& stInviterHole = mapNewUserLoginPkt[sockfd];
			
	PACKET_TYPE *pePacketType = (PACKET_TYPE*)data;
	assert ( pePacketType );
	switch ( *pePacketType ) {
	case PACKET_TYPE_REQUEST_CONN_CLIENT:
		{// 客户端A要求与客户端B建立直接的TCP连接
			log ("[hole-%d] tagReqConnClientPkt:%d size:%d", sockfd, sizeof(tagReqConnClientPkt), size);
			assert ( size == sizeof(tagReqConnClientPkt) );
			tagReqConnClientPkt *pReqConnClientPkt = (tagReqConnClientPkt *)data;
			assert ( pReqConnClientPkt );

			if (mapNewUserLoginPkt.find(pReqConnClientPkt->dwInviterID) == mapNewUserLoginPkt.end()) {
				log ( "[hole-%d] not found inviterID[%d]", sockfd, pReqConnClientPkt->dwInviterID);
				return;
			}
			tagNewUserLoginPkt& stInviter = mapNewUserLoginPkt[pReqConnClientPkt->dwInviterID];
			if (mapNewUserLoginPkt.find(pReqConnClientPkt->dwInvitedID) == mapNewUserLoginPkt.end()) {
				log ( "[hole-%d] not found invitedID[%d]", sockfd, pReqConnClientPkt->dwInvitedID);
				return;
			}
			tagNewUserLoginPkt& stInvited = mapNewUserLoginPkt[pReqConnClientPkt->dwInvitedID];

			// 客户端A想要和客户端B建立直接的TCP连接，服务器负责将A的外部IP和端口号告诉给B
			tagSrvReqMakeHolePkt SrvReqMakeHolePkt;
			SrvReqMakeHolePkt.dwInviterID = stInviter.dwID;
			SrvReqMakeHolePkt.dwInvitedID = stInvited.dwID;
			strcpy ( SrvReqMakeHolePkt.szClientHoleIP, stInviterHole.szClientIP );
			SrvReqMakeHolePkt.nClientHolePort = stInviterHole.nClientPort;
			SrvReqMakeHolePkt.dwInviterHoleID = stInviterHole.dwID;
			if (netservice::instance->datasend(SrvReqMakeHolePkt.dwInvitedID, (char*)&SrvReqMakeHolePkt, (int)sizeof(tagSrvReqMakeHolePkt))) {
				log ( "[hole-%d] %s:%d:%d invite %s:%d:%d connect %s:%d:%d", sockfd, stInviter.szClientIP, stInviter.nClientPort,stInviter.dwID, stInvited.szClientIP, stInvited.nClientPort, stInvited.dwID, stInviterHole.szClientIP, stInviterHole.nClientPort, stInviterHole.dwID );
			}
			break;
		}
	case PACKET_TYPE_REQUEST_DISCONNECT:
		{// 被动端（客户端B）请求服务器断开连接，这个时候应该将客户端B的外部IP和端口号告诉客户端A，并让客户端A主动
		 // 连接客户端B的外部IP和端口号
		 	log ("[hole-%d] tagReqSrvDisconnectPkt:%d size:%d", sockfd, sizeof(tagReqSrvDisconnectPkt), size);
			assert ( size == sizeof(tagReqSrvDisconnectPkt) );
			tagReqSrvDisconnectPkt *pReqSrvDisconnectPkt = (tagReqSrvDisconnectPkt*)data;
			assert ( pReqSrvDisconnectPkt );
			
			tagDirectConnect *pDirectConnect = new tagDirectConnect();
			pDirectConnect->param = this;
			pDirectConnect->sockfd = pReqSrvDisconnectPkt->dwInviterHoleID;
			pDirectConnect->SrvReqDirectConnectPkt.dwInvitedID = pReqSrvDisconnectPkt->dwInvitedID;
			strcpy ( pDirectConnect->SrvReqDirectConnectPkt.szInvitedIP, stInviterHole.szClientIP );
			pDirectConnect->SrvReqDirectConnectPkt.nInvitedPort = stInviterHole.nClientPort;
			
			log ( "[hole-%d] Client request disconnect", sockfd );
			netservice::instance->stopconnect(sockfd);
	
			pthread_t pthreadId = 0;
			pthread_create(&pthreadId, NULL, ThreadProcDisConnect, (void*)pDirectConnect);
			break;
		}
	default: 
		{
			log ("[hole-%d] size:%d", sockfd, size);
			assert(!bholeserverrecv);
			break;
		}
	}
}

void ppserver::ProcDisConnect(void* param)
{
	tagDirectConnect *pDirectConnect = (tagDirectConnect *)param;
	assert ( pDirectConnect );
	int sockfd = pDirectConnect->sockfd;
	tagSrvReqDirectConnectPkt SrvReqDirectConnectPkt;
	memcpy ( &SrvReqDirectConnectPkt, &(pDirectConnect->SrvReqDirectConnectPkt), sizeof(tagSrvReqDirectConnectPkt) );
	delete pDirectConnect;

	// 等待客户端B打洞完成，完成以后通知客户端A直接连接客户端B外部IP和端口号
	sem_t sem;
	if( sem_init(&sem, 0, 0) ) {
		log ( "[hole-%d] sem_init error[%d]:%s", sockfd, errno, strerror(errno));
		return;
	}
	mapSem[SrvReqDirectConnectPkt.dwInvitedID] = sem;
	sem_wait(&mapSem[SrvReqDirectConnectPkt.dwInvitedID]);
	mapSem.erase(mapSem.find(SrvReqDirectConnectPkt.dwInvitedID));
	sem_destroy(&sem);
	netservice::instance->datasend(sockfd, (char*)&SrvReqDirectConnectPkt, (int)sizeof(tagSrvReqDirectConnectPkt));
}

logcallback logfun = 0;
void log(int level, const char* format, ...)
{
	if (logfun)
	{
		va_list valst;
		va_start(valst,format);
		loglevel(level, format, valst);
		va_end(valst);
	}
}

void log(const char* format, ...)
{
	if (logfun)
	{
		va_list valst;
		va_start(valst,format);
		loglevel(0, format, valst);
		va_end(valst);
	}
}

void loglevel(int level, const char* format, va_list valst)
{
	if (logfun)
	{
		logfun(level, format, valst);
	}
}
}
