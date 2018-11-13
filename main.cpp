#include "waitsignal.h"
#include "netservice.h"
#include "log.h"
#include <vector>
#include <signal.h>
#include <execinfo.h>
#include <stdlib.h>
#include <tinyxml.h>
#include <map>

#define LISTENPORT 23531
#define ASSISTPORT 34892

#define HOSTLEN   16
#define USERCOUNT 5
#define HOSTCOUNT 5
#define PORTCOUNT 5

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

void recvlisten(int sockfd, const char* buf);
void recvassist(int sockfd, const char* buf);
void recva(int sockfd, const char* buf);
void recvb(int sockfd, const char* buf);
void WidebrightSegvHandler(int signum);
bool LoadConfig(const char* xmlfile, char* serverip, tagHost& host, std::vector<netservice::tagConfig>& vecConfig);

int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN); // ignore SIGPIPE
	signal(SIGBUS, WidebrightSegvHandler); // 总线错误 
	signal(SIGSEGV, WidebrightSegvHandler); // SIGSEGV，非法内存访问 
	signal(SIGFPE, WidebrightSegvHandler); // SIGFPE，数学相关的异常，如被0除，浮点溢出，等等
	signal(SIGABRT, WidebrightSegvHandler); // SIGABRT，由调用abort函数产生，进程非正常退出
    
	log("main start ok");
	char serverip[HOSTLEN];
	std::vector<netservice::tagConfig> vecConfig;
	char xmlname[32] = {0};
	sprintf(xmlname,"%s.xml",argv[0]);
	tagHost host;
	LoadConfig(xmlname, serverip, host, vecConfig);
	
	netservice::logfun = log;
	netservice::inst();
	if (host.type == 0) {
		netservice::tcp->startserver(LISTENPORT, recvlisten);
		netservice::tcp->startserver(ASSISTPORT, recvassist);
	} else {
		log("server:%s", serverip);
		log("uid:%d, type:%d", host.uid, host.type);
		if (host.type == 1) {
			int sockfd = netservice::tcp->startconnect(serverip, LISTENPORT, recva);
			if (0 < sockfd) {
				host.cmd = 1;
				netservice::tcp->datasend(sockfd, (char*)&host, sizeof(host));
			}
			netservice::tcp->startservertrans(serverip, ASSISTPORT, vecConfig);
		} else if (host.type == 2) {
			int sockfd = netservice::tcp->startconnect(serverip, LISTENPORT, recvb);
			if (0 < sockfd) {
				host.cmd = 2;
				netservice::tcp->datasend(sockfd, (char*)&host, sizeof(host));
			}
		}
	}

	waitsignal();
	netservice::inst(0);
	waitsignal();
	return 0;
}

void recvlisten(int sockfd, const char* buf)
{
	tagBase* pbase = (tagBase*)buf;
	if (pbase->cmd == 1) {
		tagHost st;
		memcpy(&st, buf, sizeof(st));
		mapHost[sockfd] = st;
	} else if (pbase->cmd == 2) {
		int sendsize = mapHost.size() * sizeof(tagHostPort);
		char* sendbuf = new char(sendsize);
		std::map<int, tagHost>::iterator itermap;
		int i = 0;
		for (itermap = mapHost.begin(); itermap != mapHost.end(); ++itermap,++i) {
			memcpy(sendbuf + i*sizeof(tagHostPort),(tagHostPort*)&(*itermap),sizeof(tagHostPort));
		}
		netservice::tcp->datasend(sockfd, sendbuf, sendsize);
	}
}

void recvassist(int sockfd, const char* buf)
{
	tagBase* pbase = (tagBase*)buf;
	if (pbase->cmd == 1) {
		/*tagAssistPort st;
		memcpy(&stRecv, buf, sizeof(stRecv));
		mapAssistPort[sockfd] = stRecv;*/
	}
}

void recva(int sockfd, const char* buf)
{
	
}

void recvb(int sockfd, const char* buf)
{
	
}

void WidebrightSegvHandler(int signum)  
{  
	//addr2line -e ./netservice 0x4039e8
    void *array[10];  
    size_t size;
    char **strings;  
    size_t i;  
  
    signal(signum, SIG_DFL); /* 还原默认的信号处理handler */  
  
    size = backtrace (array, 10);  
    strings = (char **)backtrace_symbols (array, size);  
  
    fprintf(stderr, "widebright received SIGSEGV! Stack trace:\n");  
    for (i = 0; i < size; i++) {  
        fprintf(stderr, "%d %s \n", (int)i, strings[i]);  
    }  
      
    free (strings);
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
