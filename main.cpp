#include "waitsignal.h"
#include "netservice.h"
#include "log.h"
#include <vector>
#include <signal.h>
#include <execinfo.h>
#include <stdlib.h>
#include <tinyxml.h>
#include <map>
#include "global.h"

#define HOSTLEN   16
#define USERCOUNT 5
#define HOSTCOUNT 5
#define PORTCOUNT 5

char* appname;

struct tagInfo {
	int uid;
	int type;
	tagInfo():uid(0),type(0) {}
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

std::map<int, tagHost> mapHost;
std::map<int, tagProxyConfig> mapAssistPort;
char serverip[HOSTLEN];
tagHost host;
tagWelcomePkt gWelcomePkt;

void mainconnectrecv(int sockfd, const char* data, int size);
void newuserholerecv(int sockfd, const char* data, int size);
void connectrecv(int sockfd, const char* data, int size);
void listenrecv(int sockfd, const char* data, int size);
void recvbl(int sockfd, const char* data, int size);
void recvba(int sockfd, const char* data, int size);

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
		//netservice::tcp->startserver(SRVTCPMAINPORT, recvlisten);
		//netservice::tcp->startserver(SRVTCPHOLEPORT, recvassist);
	} else {
		log("server:%s", serverip);
		log("uid:%d, type:%d", host.uid, host.type);
		if (host.type == 1) {
			netservice::tcp->startconnect(serverip, SRVTCPMAINPORT, mainconnectrecv);
		} else if (host.type == 2) {
		}
	}

	waitsignal();
	netservice::inst(0);
	waitsignal();
	return 0;
}

void recvlisten(int sockfd, const char* data, int size)
{
	tagBase* pbase = (tagBase*)data;
	if (pbase->cmd == 1) {
		tagHost st;
		memcpy(&st, data, sizeof(st));
		mapHost[sockfd] = st;
	} else if (pbase->cmd == 2) {
		std::map<int, tagHost>::iterator itermap;
		int i = 0;
		tagProxyConfig stProxyConfig;
		stProxyConfig.cmd = pbase->cmd;
		for (itermap = mapHost.begin(); itermap != mapHost.end(); ++itermap,++i) {
			memcpy(&(stProxyConfig.st[i]), &(*itermap),sizeof(tagHostPort));
		}
		netservice::tcp->datasend(sockfd, (const char*)&stProxyConfig, sizeof(stProxyConfig));
	} else if (pbase->cmd == 3) {
		
	}
}

void HandleNewUserLogin(int sockfd, tagNewUserLoginPkt* pNewUserLoginPkt)
{
	log ( "[A say] new user login server %s:%d:%d", pNewUserLoginPkt->szClientIP, pNewUserLoginPkt->nClientPort, pNewUserLoginPkt->dwID );

	// 创建打洞Socket，连接服务器协助打洞的端口号 SRVTCPHOLEPORT
	int sockhole = netservice::tcp->startconnect(serverip, SRVTCPHOLEPORT, newuserholerecv);
	if (0 > sockhole) return;
	
	struct sockaddr_in addr;
	socklen_t socklen = sizeof(struct sockaddr);
	bzero(&addr, sizeof(struct sockaddr_in));getsockname(sockhole, (struct sockaddr *)(&addr), &socklen);
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
			ptr = elem->Attribute("id");
			if (ptr) ptr = (strcmp(ptr, "") == 0) ? "0" : ptr;
			host.uid = atoi(ptr);
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
