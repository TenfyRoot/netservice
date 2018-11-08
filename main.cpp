#include "waitsignal.h"
#include "netservice.h"
#include "log.h"
#include <vector>

int main()
{
	log("main start ok");
	std::vector<netservice::tagConfig> vecConfig;
	netservice::tagConfig config;
	config.portto = 80;
	vecConfig.push_back(config);
	config.portto = 22;
	vecConfig.push_back(config);
	
	netservice::logfun = log;
	netservice::inst();
	//netservice::tcp->startserver(19999);
	netservice::tcp->startmakehole("127.0.0.1",19999,vecConfig);

	waitsignal();
	netservice::inst(0);
	waitsignal();
	return 0;
}
