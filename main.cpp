#include "waitsignal.h"
#include "netservice.h"
#include "log.h"
#include <vector>
#include <signal.h>
#include <execinfo.h>
#include <stdlib.h>

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

int main()
{
    signal(SIGPIPE, SIG_IGN); // ignore SIGPIPE
	signal(SIGBUS, WidebrightSegvHandler); // 总线错误 
	signal(SIGSEGV, WidebrightSegvHandler); // SIGSEGV，非法内存访问 
	signal(SIGFPE, WidebrightSegvHandler); // SIGFPE，数学相关的异常，如被0除，浮点溢出，等等
	signal(SIGABRT, WidebrightSegvHandler); // SIGABRT，由调用abort函数产生，进程非正常退出
    
	log("main start ok");
	std::vector<netservice::tagConfig> vecConfig;
	//vecConfig.push_back(netservice::tagConfig("192.168.100.103"));
	//vecConfig.push_back(netservice::tagConfig(22));
	vecConfig.push_back(netservice::tagConfig(80));
	
	netservice::logfun = log;
	netservice::inst();
	if (false)
		netservice::tcp->startserver(19999);
	else
		netservice::tcp->startservertrans("127.0.0.1",19999,vecConfig);

	waitsignal();
	netservice::inst(0);
	waitsignal();
	return 0;
}
