#include "waitsignal.h"
#include "netservice.h"
#include "log.h"
#include <vector>
#include <signal.h>
#include <execinfo.h>
#include <stdlib.h>
#include <tinyxml.h>
#include <map>
#include <arpa/inet.h>
#include "global.h"

#define HOSTLEN   16
#define USERCOUNT 5
#define HOSTCOUNT 5
#define PORTCOUNT 5

char* appname;

struct tagInfo {
	int type;
	tagInfo():type(0) {}
};

struct tagBase : tagInfo {
	int cmd;
	tagBase():cmd(2) {}
};

struct tagHostPort {
	char publichost[HOSTLEN];
	int  port[PORTCOUNT];
	tagHostPort() {
		bzero(publichost,sizeof(publichost));
		bzero(port,sizeof(int)*sizeof(port));
	}
};

struct tagHost : tagBase {
	tagHostPort st;
};

struct tagProxyConfig : tagBase {
	tagHostPort st[HOSTCOUNT-1];
};

struct tagConnectClient {
	int sockfd;
	tagReqConnClientPkt ReqConnClientPkt;
};

std::map<int, tagHost> mapHost;
std::map<int, tagProxyConfig> mapAssistPort;
char serverip[HOSTLEN];
tagHost host;
tagWelcomePkt gWelcomePkt;
std::map<int, tagNewUserLoginPkt> mapNewUserLoginPkt;
std::map<int, sem_t> mapSem;

void mainserveraccpet(int socklisten, int sockaccept);
void holeserveraccpet(int socklisten, int sockaccept);
void mainserverrecv(int sockfd, const char* data, int size);
void holeserverrecv(int sockfd, const char* data, int size);
void mainconnectrecv(int sockfd, const char* data, int size);
void newuserholerecv(int sockfd, const char* data, int size);
void connectrecv(int sockfd, const char* data, int size);
void listenrecv(int sockfd, const char* data, int size);

void* ThreadProcConnectClient(void* lpParameter);
void* ThreadProcListenHole(void* lpParameter);
void* ThreadProcMakeHole(void* lpParameter);
void HandleNewUserLogin(int sockfd, tagNewUserLoginPkt* pNewUserLoginPkt);
void HandleSrvReqMakeHole(int sockfd, tagSrvReqMakeHolePkt* pSrvReqMakeHolePkt);

void WidebrightSegvHandler(int signum);
bool LoadConfig(const char* xmlfile, char* serverip, tagHost& host, std::vector<netservice::tagConfig>& vecConfig);

int main(int argc, char *argv[])
{
	appname = argv[0];
    signal(SIGPIPE, SIG_IGN); // ignore SIGPIPE
	signal(SIGBUS, WidebrightSegvHandler); // 总线错误 
	signal(SIGSEGV, WidebrightSegvHandler); // SIGSEGV，非法内存访问 
	signal(SIGFPE, WidebrightSegvHandler); // SIGFPE，数学相关的异常，如被0除，浮点溢出，等等
	signal(SIGABRT, WidebrightSegvHandler); // SIGABRT，由调用abort函数产生，进程非正常退出
    
	log("main start ok");	
	std::vector<netservice::tagConfig> vecConfig;
	char xmlname[32] = {0};
	sprintf(xmlname,"./%s.xml",appname);
	LoadConfig(xmlname, serverip, host, vecConfig);
	netservice::logfun = log;
	netservice::inst();
	log ("tagWelcomePkt:%d, tagNewUserLoginPkt:%d, tagSrvReqMakeHolePkt:%d",
		sizeof(tagWelcomePkt),
		sizeof(tagNewUserLoginPkt),
		sizeof(tagSrvReqMakeHolePkt));
	if (host.type == 0) {
		netservice::tcp->startserver(SRVTCPMAINPORT, mainserverrecv, mainserveraccpet);
		netservice::tcp->startserver(SRVTCPHOLEPORT, holeserverrecv, holeserveraccpet);
	} else {
		log("server:%s", serverip);
		log("type:%d", host.type);
		if (host.type == 1) {
			netservice::tcp->startconnect(serverip, SRVTCPMAINPORT, mainconnectrecv);
		}
	}

	waitsignal();
	netservice::inst(0);
	waitsignal();
	return 0;
}

void mainserveraccpet(int socklisten, int sockaccept)
{
	struct sockaddr_in addr;
	socklen_t socklen = sizeof(struct sockaddr);
	bzero(&addr, sizeof(struct sockaddr_in)); getpeername(sockaccept, (struct sockaddr *)(&addr), &socklen);
	
	tagNewUserLoginPkt NewUserLoginPkt;
	inet_ntop(AF_INET, &addr.sin_addr, NewUserLoginPkt.szClientIP, sizeof(NewUserLoginPkt.szClientIP));
	NewUserLoginPkt.nClientPort = ntohs(addr.sin_port);
	NewUserLoginPkt.dwID = sockaccept;
	mapNewUserLoginPkt[NewUserLoginPkt.dwID] = NewUserLoginPkt;
	
	log("%s[%d] [main] client enter %s:%d", __FUNCTION__,__LINE__, NewUserLoginPkt.szClientIP, NewUserLoginPkt.nClientPort);
	
	tagWelcomePkt WelcomePkt;
	strcpy ( WelcomePkt.szClientIP, NewUserLoginPkt.szClientIP );
	WelcomePkt.nClientPort = NewUserLoginPkt.nClientPort;
	WelcomePkt.dwID = NewUserLoginPkt.dwID;
	sprintf ( WelcomePkt.szWelcomeInfo, "Hello, ID.%d, Welcome to login", WelcomePkt.dwID );
	netservice::tcp->datasend( sockaccept, (char*)&WelcomePkt, (int)sizeof(tagWelcomePkt) );
}

void holeserveraccpet(int socklisten, int sockaccept)
{
	struct sockaddr_in addr;
	socklen_t socklen = sizeof(struct sockaddr);
	bzero(&addr, sizeof(struct sockaddr_in)); getpeername(sockaccept, (struct sockaddr *)(&addr), &socklen);
	
	tagNewUserLoginPkt NewUserLoginPkt;
	inet_ntop(AF_INET, &addr.sin_addr, NewUserLoginPkt.szClientIP, sizeof(NewUserLoginPkt.szClientIP));
	NewUserLoginPkt.nClientPort = ntohs(addr.sin_port);
	NewUserLoginPkt.dwID = sockaccept;
	mapNewUserLoginPkt[NewUserLoginPkt.dwID] = NewUserLoginPkt;
	
	log("%s[%d] [hole] client enter %s:%d", __FUNCTION__,__LINE__, NewUserLoginPkt.szClientIP, NewUserLoginPkt.nClientPort);
}

void mainserverrecv(int sockfd, const char* data, int size)
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
			log ( "[main] Client.%d hole and listen ready", pHoleListenReadyPkt->dwInvitedID );
			
			// 通知正在与客户端A通信的服务器线程，以将客户端B的外部IP地址和端口号告诉客户端A
			
		}
	default:
		{
			assert(!bmainserverrecv);
			break;
		}
	}
}

void holeserverrecv(int sockfd, const char* data, int size)
{
	if ( !data || size < 4 ) return;
	bool bholeserverrecv = true;
	
	PACKET_TYPE *pePacketType = (PACKET_TYPE*)data;
	assert ( pePacketType );
	switch ( *pePacketType ) {
	case PACKET_TYPE_REQUEST_CONN_CLIENT:
		{// 客户端A要求与客户端B建立直接的TCP连接
			log ("[hole] tagReqConnClientPkt:%d size:%d", sizeof(tagReqConnClientPkt), size);
			assert ( size == sizeof(tagReqConnClientPkt) );
			tagReqConnClientPkt *pReqConnClientPkt = (tagReqConnClientPkt *)data;
			assert ( pReqConnClientPkt );
			
			tagConnectClient *pConnectClient = new tagConnectClient();
			pConnectClient->sockfd = sockfd;
			memcpy(&(pConnectClient->ReqConnClientPkt), pReqConnClientPkt, sizeof(tagReqConnClientPkt));
			pthread_t pthreadId = 0;
			pthread_create(&pthreadId, NULL, ThreadProcConnectClient, (void*)pConnectClient);
		}
	case PACKET_TYPE_REQUEST_DISCONNECT:
		{// 被动端（客户端B）请求服务器断开连接，这个时候应该将客户端B的外部IP和端口号告诉客户端A，并让客户端A主动
		 // 连接客户端B的外部IP和端口号
		 	log ("[hole] tagReqSrvDisconnectPkt:%d size:%d", sizeof(tagReqSrvDisconnectPkt), size);
			assert ( size == sizeof(tagReqSrvDisconnectPkt) );
			tagReqSrvDisconnectPkt *pReqSrvDisconnectPkt = (tagReqSrvDisconnectPkt*)data;
			assert ( pReqSrvDisconnectPkt );
			log ( "[hole] Client.%d request disconnect", stInviterHole.dwID );
			netservice::tcp->stopconnect(sockfd);
			
			/*if (mapNewUserLoginPkt.find(pReqConnClientPkt->dwInvitedID) == mapNewUserLoginPkt.end()) {
				log ( "[hole] not found invitedID[%d]", pReqConnClientPkt->dwInvitedID);
				return;
			}
			tagNewUserLoginPkt& stInvited = mapNewUserLoginPkt[pReqConnClientPkt->dwInvitedID];
			
			tagSrvReqDirectConnectPkt SrvReqDirectConnectPkt;
			SrvReqDirectConnectPkt.dwInvitedID = stInvited.dwID;
			strcpy ( SrvReqDirectConnectPkt.szInvitedIP, stInvited.szClientIP );
			SrvReqDirectConnectPkt.nInvitedPort = stInvited.nClientPort;*/
			
		}
	default: 
		{
			assert(!bholeserverrecv);
			break;
		}
	}
}

void* ThreadProcConnectClient(void* lpParameter)
{
	tagConnectClient *pConnectClient = (tagConnectClient*)lpParameter;
	assert ( pConnectClient );
	tagConnectClient ConnectClient;
	memcpy ( &ConnectClient, pConnectClient, sizeof(tagConnectClient) );
	delete pConnectClient;
	
	if (mapNewUserLoginPkt.find(pConnectClient->sockfd) == mapNewUserLoginPkt.end()) {
		log ( "[hole] not found inviterHoleID[%d]", pConnectClient->sockfd);
		return;
	}
	tagNewUserLoginPkt& stInviterHole = mapNewUserLoginPkt[sockfd];
	
	tagReqConnClientPkt& ReqConnClientPkt = pConnectClient->ReqConnClientPkt;	
	if (mapNewUserLoginPkt.find(ReqConnClientPkt.dwInviterID) == mapNewUserLoginPkt.end()) {
		log ( "[hole] not found inviterID[%d]", ReqConnClientPkt.dwInviterID);
		return 0;
	}
	tagNewUserLoginPkt& stInviter = mapNewUserLoginPkt[ReqConnClientPkt.dwInviterID];
	if (mapNewUserLoginPkt.find(ReqConnClientPkt.dwInvitedID) == mapNewUserLoginPkt.end()) {
		log ( "[hole] not found invitedID[%d]", ReqConnClientPkt.dwInvitedID);
		return 0;
	}
	tagNewUserLoginPkt& stInvited = mapNewUserLoginPkt[ReqConnClientPkt.dwInvitedID];

	// 客户端A想要和客户端B建立直接的TCP连接，服务器负责将A的外部IP和端口号告诉给B
	tagSrvReqMakeHolePkt SrvReqMakeHolePkt;
	SrvReqMakeHolePkt.dwInviterID = stInviter.dwID;
	SrvReqMakeHolePkt.dwInvitedID = stInvited.dwID;
	strcpy ( SrvReqMakeHolePkt.szClientHoleIP, stInviterHole.szClientIP );
	SrvReqMakeHolePkt.nClientHolePort = stInviterHole.nClientPort;
	SrvReqMakeHolePkt.dwInviterHoleID = stInviterHole.dwID;
	netservice::tcp->datasend( sockfd, (char*)&SrvReqMakeHolePkt, (int)sizeof(tagSrvReqMakeHolePkt) );
	
	log ( "[hole] %s:%d invite %s:%d connect %s:%d", stInviter.szClientIP, stInviter.nClientPort, stInvited.szClientIP, stInvited.nClientPort, stInviterHole.szClientIP, stInviterHole.nClientPort );
	
	// 等待客户端B打洞完成，完成以后通知客户端A直接连接客户端B外部IP和端口号
	sem_t sem;
	if( sem_init(&sem, 0, 0) ) {
		log ( "[hole] sem_init error[%d]:%s", errno, strerror(errno));
		return 0;
	}
	mapSem[pConnectClient->sockfd] = sem;
	while ((ret = sem_timedwait(&mapSem[pConnectClient->sockfd], NULL)) == -1 && errno == EINTR) {
		usleep(1000);
	}
	
	
	return 0;
}

void HandleNewUserLogin(int sockfd, tagNewUserLoginPkt* pNewUserLoginPkt)
{
	log ( "[A say] new user login server %s:%d:%d", pNewUserLoginPkt->szClientIP, pNewUserLoginPkt->nClientPort, pNewUserLoginPkt->dwID );

	// 创建打洞Socket，连接服务器协助打洞的端口号 SRVTCPHOLEPORT
	int sockhole = netservice::tcp->startconnect(serverip, SRVTCPHOLEPORT, newuserholerecv);
	if (0 > sockhole) return;
	
	struct sockaddr_in addr;
	socklen_t socklen = sizeof(struct sockaddr);
	bzero(&addr, sizeof(struct sockaddr_in)); getpeername(sockhole, (struct sockaddr *)(&addr), &socklen);
	int holeport = ntohs(addr.sin_port);
	
	// 创建一个线程来侦听 打洞端口 的连接请求
	pthread_t pthreadId = 0;
	pthread_create(&pthreadId, NULL, ThreadProcListenHole, (void*)&holeport);
	
	// 我（客户端A）向服务器协助打洞的端口号 SRVTCPHOLEPORT 发送申请，希望与新登录的客户端B建立连接
	// 服务器会将我的打洞用的外部IP和端口号告诉客户端B
	assert ( gWelcomePkt.dwID > 0 );
	tagReqConnClientPkt ReqConnClientPkt;
	ReqConnClientPkt.dwInviterID = gWelcomePkt.dwID;
	ReqConnClientPkt.dwInvitedID = pNewUserLoginPkt->dwID;
	netservice::tcp->datasend( sockhole, (char*)&ReqConnClientPkt, (int)sizeof(tagReqConnClientPkt) );
}

void HandleSrvReqMakeHole(int sockfd, tagSrvReqMakeHolePkt* pSrvReqMakeHolePkt)
{
	assert ( pSrvReqMakeHolePkt );
	int sockhole = netservice::tcp->startconnect(serverip, SRVTCPHOLEPORT);
	if (0 > sockhole) return;
	
	struct sockaddr_in addr;
	socklen_t socklen = sizeof(struct sockaddr);
	bzero(&addr, sizeof(struct sockaddr_in));getsockname(sockhole, (struct sockaddr *)(&addr), &socklen);
	int holeport = ntohs(addr.sin_port);
	
	tagReqSrvDisconnectPkt ReqSrvDisconnectPkt;
	ReqSrvDisconnectPkt.dwInviterID 	= pSrvReqMakeHolePkt->dwInvitedID;
	ReqSrvDisconnectPkt.dwInviterHoleID = pSrvReqMakeHolePkt->dwInviterHoleID;
	ReqSrvDisconnectPkt.dwInvitedID 	= pSrvReqMakeHolePkt->dwInvitedID;
	assert ( ReqSrvDisconnectPkt.dwInvitedID == gWelcomePkt.dwID );
	
	// 连接服务器协助打洞的端口号 SRV_TCP_HOLE_PORT，发送一个断开连接的请求，然后将连接断开，服务器在收到这个包的时候也会将连接断开
	netservice::tcp->datasend( sockhole, (char*)&ReqSrvDisconnectPkt, (int)sizeof(tagReqSrvDisconnectPkt) );
	netservice::tcp->stopconnect(sockhole);
	
	tagSrvReqMakeHolePkt *pSrvReqMakeHolePktNew = new tagSrvReqMakeHolePkt;
	if ( !pSrvReqMakeHolePktNew ) return;
	memcpy ( pSrvReqMakeHolePktNew, pSrvReqMakeHolePkt, sizeof(tagSrvReqMakeHolePkt) );
	pSrvReqMakeHolePktNew->nBindPort = holeport;
	
	// 创建一个线程来向客户端A的外部IP地址、端口号打洞
	pthread_t pthreadId = 0;
	pthread_create(&pthreadId, NULL, ThreadProcMakeHole, (void*)pSrvReqMakeHolePktNew);
}

void HandleSrvReqDirectConnect(tagSrvReqDirectConnectPkt* pSrvReqDirectConnectPkt)
{
	assert ( pSrvReqDirectConnectPkt );
	log ( "[A say] you can connect direct to %s:%d:%u \n", pSrvReqDirectConnectPkt->szInvitedIP,
		pSrvReqDirectConnectPkt->nInvitedPort, pSrvReqDirectConnectPkt->dwInvitedID );
}

void* ThreadProcMakeHole(void* lpParameter)
{
	tagSrvReqMakeHolePkt *pSrvReqMakeHolePkt = (tagSrvReqMakeHolePkt*)lpParameter;
	assert ( pSrvReqMakeHolePkt );
	tagSrvReqMakeHolePkt SrvReqMakeHolePkt;
	memcpy ( &SrvReqMakeHolePkt, pSrvReqMakeHolePkt, sizeof(tagSrvReqMakeHolePkt) );
	delete pSrvReqMakeHolePkt; pSrvReqMakeHolePkt = NULL;
	
	log ( "[B say] server request make hole to %s:%d:%d", SrvReqMakeHolePkt.szClientHoleIP,\
		SrvReqMakeHolePkt.nClientHolePort, SrvReqMakeHolePkt.dwInviterID );
	
	// 创建Socket，本地端口绑定到 nBindPort，连接客户端A的外部IP和端口号（这个连接往往会失败）
	int sockfd = netservice::tcp->startconnect(SrvReqMakeHolePkt.szClientHoleIP, SrvReqMakeHolePkt.nClientHolePort, connectrecv, SrvReqMakeHolePkt.nBindPort);
	if (0 > sockfd) return 0;
	log ( "[B say] connect success when make hole. receiving data ..." );

	const char* buf = "[B say] hello!";
	netservice::tcp->datasend( sockfd, buf, (int)strlen(buf) );
	
	// 创建一个线程来侦听的连接请求
	pthread_t pthreadIdListen = 0;
	pthread_create(&pthreadIdListen, NULL, ThreadProcListenHole, (void*)&SrvReqMakeHolePkt.nBindPort);
	return 0;
}

void* ThreadProcListenHole(void* lpParameter)
{
	int holeport = *((int*)(lpParameter));
	log ( "listen at port %d", holeport );
	netservice::tcp->startserver(holeport, listenrecv);
	return 0;
}

void mainconnectrecv(int sockfd, const char* data, int size)
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
			memcpy ( &gWelcomePkt, pWelcomePkt, sizeof(tagWelcomePkt) );
			assert ( gWelcomePkt.dwID > 0 );
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

void newuserholerecv(int sockfd, const char* data, int size)
{
	// 等待服务器回应，将客户端B的外部IP地址和端口号告诉我（客户端A）
	assert ( size == sizeof(tagSrvReqDirectConnectPkt) );
	PACKET_TYPE *pePacketType = (PACKET_TYPE*)data;
	assert ( pePacketType && *pePacketType == PACKET_TYPE_TCP_DIRECT_CONNECT );
	usleep ( 1000000 );
	HandleSrvReqDirectConnect ( (tagSrvReqDirectConnectPkt*)data );
	log ( "[A say] HandleSrvReqDirectConnect end" );
}

void listenrecv(int sockfd, const char* data, int size)
{
	log ( "[A say] recv from B: %s", data );
	char buf[256];
	sprintf(buf, "reply: %s", data);
	netservice::tcp->datasend( sockfd, buf, (int)strlen(buf) );
}

void connectrecv(int sockfd, const char* data, int size)
{
	log ( "[B say] recv from A: %s", data );
}

void WidebrightSegvHandler(int signum)  
{  
	//addr2line -e ./netservice 0x4039e8
    void *array[15];
    size_t size;
    char **strings;  
    size_t i; 
  
    signal(signum, SIG_DFL); /* 还原默认的信号处理handler */
  
    size = backtrace (array, 15);
    strings = (char **)backtrace_symbols (array, size);  
  
  	FILE *fp; 
    char buffer[256];
    char cmdbuf[128];
    fprintf(stderr, "widebright received SIGSEGV! Stack trace:\n"); 
    //for (i = 0; i < size; i++) fprintf(stderr, "%d %s \n", (int)i, strings[i]);
    for (i = 0; i < size; i++) {
    	if (strstr(strings[i], appname) == NULL) continue;
    	if (strstr(strings[i], "WidebrightSegvHandler") != NULL) continue;
    	sprintf(cmdbuf, "echo '%s' | cut -d '[' -f2 | cut -d ']' -f1", strings[i]);
    	fp=popen(cmdbuf, "r");
    	fgets(buffer, sizeof(buffer), fp);
    	pclose(fp);
    	sprintf(cmdbuf, "addr2line -e %s %s", appname, buffer);
    	fp=popen(cmdbuf, "r");
    	fgets(buffer, sizeof(buffer), fp);
        fprintf(stderr, "%d %s", (int)i, buffer);
        pclose(fp);
    }
 
    free (strings);
    getchar();
    exit(0);
}

bool LoadConfig(const char* xmlfile, char* serverip, tagHost& host, std::vector<netservice::tagConfig>& vecConfig)
{
	TiXmlDocument doc;
	if (!doc.LoadFile(xmlfile)) {
		//log(3, "%s",doc.ErrorDesc());
		return false;
	}
	TiXmlElement* root = doc.FirstChildElement();
	if (NULL == root) {
		log(3, "Failed to load file: No root element.");
		doc.Clear();
		return false;
	}
	
	/*
	<config>
		<server ip="127.0.0.1"/> 
		<Item port="1"/> 
		<Item ip="192.168.100.119"/> 
	</config>
	*/
	for (TiXmlElement* elem = root->FirstChildElement(); NULL != elem; elem = elem->NextSiblingElement()) {
		const char* ptr;
		if (strcmp(elem->Value(), "server") == 0) {
			ptr = elem->Attribute("ip");
			if (ptr) ptr = (strcmp(ptr, "") == 0) ? "127.0.0.1" : ptr;
			strcpy(serverip, ptr);
			continue;
		}
		if (strcmp(elem->Value(), "info") == 0) {
			ptr = elem->Attribute("type");
			if (ptr) ptr = (strcmp(ptr, "") == 0) ? "0" : ptr;
			host.type = atoi(ptr);
			ptr = elem->Attribute("publichost");
			if (ptr) strcpy(host.st.publichost, ptr);
			continue;
		}
		
		ptr = elem->Attribute("port");
		if (ptr) ptr = (strcmp(ptr, "") == 0) ? NULL : ptr;
		int port = ptr ? atoi(ptr) : 0;
		ptr = elem->Attribute("ip");
		if (ptr) ptr = (strcmp(ptr, "") == 0) ? NULL : ptr;
		if (ptr && port) {
			vecConfig.push_back(netservice::tagConfig(ptr,port));
		} else if (ptr) {
			vecConfig.push_back(netservice::tagConfig(ptr));
		} else if (port) {
			vecConfig.push_back(netservice::tagConfig(port));
		}
	}
	return (vecConfig.size() > 0);
}
