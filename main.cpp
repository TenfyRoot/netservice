#include "waitsignal.h"
#include "ppserver.h"
#include "ppclient.h"
#include "netservice.h"
#include "log.h"
#include <vector>
#include <signal.h>
#include <execinfo.h>
#include <stdlib.h>
#include <tinyxml.h>
#include <map>

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

std::map<int, tagHost> mapHost;
std::map<int, tagProxyConfig> mapAssistPort;
char serverip[HOSTLEN];
tagHost host;

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
	ppserver::logfun = log;
	ppserver::inst();
	ppclient::logfun = log;
	ppclient::inst();
	if (host.type == 0) {
		ppserver::instance->startserver();
	} else {
		log("server:%s", serverip);
		log("type:%d", host.type);
		if (host.type == 1) {
			ppclient::instance->startconnect(serverip);
		}
	}

	waitsignal();
	netservice::inst(0);
	waitsignal();
	return 0;
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
