#include "ppclient.h"
#include "netservice.h"
#include <assert.h>
#include <unistd.h>
#include <stdio.h>

namespace ppclient {

#define release(p) {if (p) {delete p; p = 0;}}

void log(const char* format, ...);
void log(int level, const char* format, ...);
void loglevel(int level, const char* format, va_list valst);

struct tagListenHole : tagParam {
	int port;
};
void* ThreadProcListenHole(void* param) {
	((ppclient*)((tagParam*)param)->param)->ProcListenHole(param); return 0;
}
struct tagMakeHole : tagParam {
	tagSrvReqMakeHolePkt SrvReqMakeHolePkt;
};
void* ThreadProcMakeHole(void* param) {
	((ppclient*)((tagParam*)param)->param)->ProcMakeHole(param); return 0;
}

void call_mainconnectrecv(void* param, int sockfd, const char* data, int size) {
	((ppclient*)(param))->mainconnectrecv(sockfd, data, size);
}
void call_newuserholerecv(void* param, int sockfd, const char* data, int size) {
	((ppclient*)(param))->newuserholerecv(sockfd, data, size);
}
void call_connectrecv(void* param, int sockfd, const char* data, int size) {
	((ppclient*)(param))->connectrecv(sockfd, data, size);
}
void call_listenrecv(void* param, int sockfd, const char* data, int size) {
	((ppclient*)(param))->listenrecv(sockfd, data, size);
}

ppclient* instance = 0;
void inst(int create) {
	if (!create) release(instance);
	if (create && !instance) {instance = new ppclient();}	
}

ppclient::ppclient()
{}

ppclient::~ppclient()
{}

int ppclient::startconnect(const char* ip)
{
	strcpy(serverip, ip);
	return netservice::instance->startconnect(ip, SRVTCPMAINPORT, call_mainconnectrecv, this);
}

void ppclient::mainconnectrecv(int sockfd, const char* data, int size)
{
	if ( !data || size < 4 ) return;
	PACKET_TYPE *pePacketType = (PACKET_TYPE*)data;
	assert ( pePacketType );
	switch ( *pePacketType )
	{
	case PACKET_TYPE_WELCOME:
		{// 收到服务器的欢迎信息，说明登录已经成功
			log ("tagWelcomePkt:%d size:%d", sizeof(tagWelcomePkt), size);
			assert ( sizeof(tagWelcomePkt) == size );
			tagWelcomePkt *pWelcomePkt = (tagWelcomePkt*)data;
			log ( "%s:%d:%d >> %s", pWelcomePkt->szClientIP, pWelcomePkt->nClientPort, pWelcomePkt->dwID, pWelcomePkt->szWelcomeInfo );
			memcpy ( &myWelcomePkt, pWelcomePkt, sizeof(tagWelcomePkt) );
			assert ( myWelcomePkt.dwID > 0 );
			break;
		}
	case PACKET_TYPE_NEW_USER_LOGIN:
		{// 其他客户端（客户端B）登录到服务器了
			log ("[A say] tagNewUserLoginPkt:%d size:%d", sizeof(tagNewUserLoginPkt), size);
			assert ( size == sizeof(tagNewUserLoginPkt) );
			HandleNewUserLogin ( sockfd, (tagNewUserLoginPkt*)data );
			break;
		}
	case PACKET_TYPE_REQUEST_MAKE_HOLE:
		{// 服务器要我（客户端B）向另外一个客户端（客户端A）打洞
			log ("[B say] tagSrvReqMakeHolePkt:%d size:%d", sizeof(tagSrvReqMakeHolePkt), size);
			assert ( size == sizeof(tagSrvReqMakeHolePkt) );
			HandleSrvReqMakeHole ( sockfd, (tagSrvReqMakeHolePkt*)data );
			break;
		}
	default: break;
	}
}

void ppclient::newuserholerecv(int sockfd, const char* data, int size)
{
	// 等待服务器回应，将客户端B的外部IP地址和端口号告诉我（客户端A）
	assert ( size == sizeof(tagSrvReqDirectConnectPkt) );
	PACKET_TYPE *pePacketType = (PACKET_TYPE*)data;
	assert ( pePacketType && *pePacketType == PACKET_TYPE_TCP_DIRECT_CONNECT );
	usleep ( 1000000 );
	tagSrvReqDirectConnectPkt* pSrvReqDirectConnectPkt = (tagSrvReqDirectConnectPkt*)data;
	assert ( pSrvReqDirectConnectPkt );
	log ( "[A say] you can connect direct to %s:%d:%d \n", pSrvReqDirectConnectPkt->szInvitedIP,
		pSrvReqDirectConnectPkt->nInvitedPort, pSrvReqDirectConnectPkt->dwInvitedID );
	log ( "[A say] HandleSrvReqDirectConnect end" );
}

void ppclient::listenrecv(int sockfd, const char* data, int size)
{
	log ( "[A say] recv from B: %s", data );
	char buf[256];
	sprintf(buf, "reply: %s", data);
	netservice::instance->datasend( sockfd, buf, (int)strlen(buf) );
}

void ppclient::connectrecv(int sockfd, const char* data, int size)
{
	log ( "[B say] recv from A: %s", data );
}

void ppclient::HandleNewUserLogin(int sockfd, tagNewUserLoginPkt* pNewUserLoginPkt)
{
	log ( "[A say] new user login server %s:%d:%d", pNewUserLoginPkt->szClientIP, pNewUserLoginPkt->nClientPort, pNewUserLoginPkt->dwID );

	// 创建打洞Socket，连接服务器协助打洞的端口号 SRVTCPHOLEPORT
	int sockhole = netservice::instance->startconnect(serverip, SRVTCPHOLEPORT, call_newuserholerecv, this);
	if (0 > sockhole) return;
	
	struct sockaddr_in addr;
	socklen_t socklen = sizeof(struct sockaddr);
	bzero(&addr, sizeof(struct sockaddr_in)); getpeername(sockhole, (struct sockaddr *)(&addr), &socklen);

	tagListenHole *pListenHole = new tagListenHole();
	pListenHole->param = this;
	pListenHole->port = ntohs(addr.sin_port);
	// 创建一个线程来侦听 打洞端口 的连接请求
	pthread_t pthreadId = 0;
	pthread_create(&pthreadId, NULL, ThreadProcListenHole, (void*)pListenHole);
	
	// 我（客户端A）向服务器协助打洞的端口号 SRVTCPHOLEPORT 发送申请，希望与新登录的客户端B建立连接
	// 服务器会将我的打洞用的外部IP和端口号告诉客户端B
	assert ( myWelcomePkt.dwID > 0 );
	tagReqConnClientPkt ReqConnClientPkt;
	ReqConnClientPkt.dwInviterID = myWelcomePkt.dwID;
	ReqConnClientPkt.dwInvitedID = pNewUserLoginPkt->dwID;
	netservice::instance->datasend( sockhole, (char*)&ReqConnClientPkt, (int)sizeof(tagReqConnClientPkt) );
}

void ppclient::HandleSrvReqMakeHole(int sockfd, tagSrvReqMakeHolePkt* pSrvReqMakeHolePkt)
{
	assert ( pSrvReqMakeHolePkt );
	int sockhole = netservice::instance->startconnect(serverip, SRVTCPHOLEPORT);
	if (0 > sockhole) return;
	
	struct sockaddr_in addr;
	socklen_t socklen = sizeof(struct sockaddr);
	bzero(&addr, sizeof(struct sockaddr_in));getsockname(sockhole, (struct sockaddr *)(&addr), &socklen);
	int holeport = ntohs(addr.sin_port);
	
	tagReqSrvDisconnectPkt ReqSrvDisconnectPkt;
	ReqSrvDisconnectPkt.dwInviterID 	= pSrvReqMakeHolePkt->dwInvitedID;
	ReqSrvDisconnectPkt.dwInviterHoleID = pSrvReqMakeHolePkt->dwInviterHoleID;
	ReqSrvDisconnectPkt.dwInvitedID 	= pSrvReqMakeHolePkt->dwInvitedID;
	assert ( ReqSrvDisconnectPkt.dwInvitedID == myWelcomePkt.dwID );
	
	// 连接服务器协助打洞的端口号 SRV_TCP_HOLE_PORT，发送一个断开连接的请求，然后将连接断开，服务器在收到这个包的时候也会将连接断开
	netservice::instance->datasend( sockhole, (char*)&ReqSrvDisconnectPkt, (int)sizeof(tagReqSrvDisconnectPkt) );
	netservice::instance->stopconnect(sockhole);
	
	tagMakeHole *pMakeHole = new tagMakeHole();
	pMakeHole->param = this;
	memcpy ( &pMakeHole->SrvReqMakeHolePkt, pSrvReqMakeHolePkt, sizeof(tagSrvReqMakeHolePkt) );
	pMakeHole->SrvReqMakeHolePkt.nBindPort = holeport;
	
	// 创建一个线程来向客户端A的外部IP地址、端口号打洞
	pthread_t pthreadId = 0;
	pthread_create(&pthreadId, NULL, ThreadProcMakeHole, (void*)pMakeHole);
}

void ppclient::ProcMakeHole(void* param)
{
	tagMakeHole *pMakeHole = (tagMakeHole *)param;
	assert(pMakeHole);
	tagMakeHole stMakeHole;
	memcpy(&stMakeHole, pMakeHole, sizeof(tagMakeHole));
	delete pMakeHole;

	tagSrvReqMakeHolePkt& SrvReqMakeHolePkt = stMakeHole.SrvReqMakeHolePkt;
	log ( "[B say] server request make hole to %s:%d:%d", SrvReqMakeHolePkt.szClientHoleIP,\
		SrvReqMakeHolePkt.nClientHolePort, SrvReqMakeHolePkt.dwInviterID );
	
	// 创建Socket，本地端口绑定到 nBindPort，连接客户端A的外部IP和端口号（这个连接往往会失败）
	int sockfd = netservice::instance->startconnect(SrvReqMakeHolePkt.szClientHoleIP, SrvReqMakeHolePkt.nClientHolePort, call_connectrecv, this, SrvReqMakeHolePkt.nBindPort);
	if (0 > sockfd) return;
	log ( "[B say] connect success when make hole. receiving data ..." );

	const char* buf = "[B say] hello!";
	netservice::instance->datasend( sockfd, buf, (int)strlen(buf) );
	
	// 创建一个线程来侦听的连接请求
	pthread_t pthreadIdListen = 0;
	pthread_create(&pthreadIdListen, NULL, ThreadProcListenHole, (void*)&SrvReqMakeHolePkt.nBindPort);
}

void ppclient::ProcListenHole(void* param)
{
	tagListenHole *pListenHole = (tagListenHole *)param;
	assert(pListenHole);
	tagListenHole stListenHole;
	memcpy(&stListenHole, pListenHole, sizeof(tagListenHole));
	delete pListenHole;

	log ( "listen at port %d", stListenHole.port );
	netservice::instance->startserver(stListenHole.port, call_listenrecv, this);
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
