#include "waitsignal.h"
#include "netservice.h"
#include "log.h"
#include <vector>
#include <signal.h>
#include <execinfo.h>
#include <stdlib.h>
#include <tinyxml.h>

struct tagCmd {
	int uid;
	int cmd;
};

struct tagHost : tagCmd{
	char domain[256];
	int ports[20];
};

struct tagAssist : tagCmd{
	int port;
};

void recvclient(int sockfd, const char* buf);
void recvassist(int sockfd, const char* buf);
void WidebrightSegvHandler(int signum);
bool LoadConfig(const char* xmlfile, char* serverip, std::vector<netservice::tagConfig>& vecConfig);


std::map<int, tagHost> Hosts;

int main()
{
    signal(SIGPIPE, SIG_IGN); // ignore SIGPIPE
	signal(SIGBUS, WidebrightSegvHandler); // 总线错误 
	signal(SIGSEGV, WidebrightSegvHandler); // SIGSEGV，非法内存访问 
	signal(SIGFPE, WidebrightSegvHandler); // SIGFPE，数学相关的异常，如被0除，浮点溢出，等等
	signal(SIGABRT, WidebrightSegvHandler); // SIGABRT，由调用abort函数产生，进程非正常退出
    
	log("main start ok");
	std::vector<netservice::tagConfig> vecConfig;
	netservice::logfun = log;
	netservice::inst();
	if (true) {
		netservice::tcp->startserver(23531, recvclient);
		netservice::tcp->startserver(34892, recvassist);
	} else {
		char serverip[16];
		//vecConfig.push_back(netservice::tagConfig("192.168.100.103"));
		//vecConfig.push_back(netservice::tagConfig(22));
		//vecConfig.push_back(netservice::tagConfig("192.168.100.103", 80));
		//vecConfig.push_back(netservice::tagConfig(80));
		LoadConfig("netservice.xml", serverip, vecConfig);
		log("server:%s", serverip);
		netservice::tcp->startservertrans(serverip,19999, vecConfig);
	}

	waitsignal();
	netservice::inst(0);
	waitsignal();
	return 0;
}

void recvclient(int sockfd, const char* buf)
{
	tagCmd* pCmd = (tagCmd*)buf;
	if (pCmd->cmd == 1) {
		tagHost stHost;
		memcpy(&stHost, buf, sizeof(tagHost));
		Hosts[sockfd] = stHost;
	} else if (pCmd->cmd == 2) {//request info
		
	}
}

void recvassist(int sockfd, const char* buf)
{
	tagCmd* pCmd = (tagCmd*)buf;
	if (pCmd->cmd == 1) {
		//tagAssist *pAssist = (tagAssist *)buf;
	}
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

bool LoadConfig(const char* xmlfile, char* serverip, std::vector<netservice::tagConfig>& vecConfig)
{
	TiXmlDocument doc;
	if (!doc.LoadFile(xmlfile)) {
		log(3, "%s",doc.ErrorDesc());
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
		if (strcmp(elem->Value(), "server") == 0) {
			const char* svrip = elem->Attribute("ip");
			if (svrip) svrip = (strcmp(svrip, "") == 0) ? "127.0.0.1" : svrip;
			strcpy(serverip, svrip);
			continue;
		}
		const char* ip = elem->Attribute("ip");
		const char* port = elem->Attribute("port");
		if (ip) ip = (strcmp(ip, "") == 0) ? NULL : ip;
		if (port) port = (strcmp(port, "") == 0) ? NULL : port;
		if (ip && port) {
			vecConfig.push_back(netservice::tagConfig(ip,atoi(port)));
		} else if (ip) {
			vecConfig.push_back(netservice::tagConfig(ip));
		} else if (port) {
			vecConfig.push_back(netservice::tagConfig(atoi(port)));
		}
	}
	return (vecConfig.size() > 0);
}
