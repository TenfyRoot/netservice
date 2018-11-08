#include "netservice.h"
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>

namespace netservice {

#define RELEASE(p) {if (p) {delete p; p = 0;}}

void log(const char* format, ...);
void log(int level, const char* format, ...);
void loglevel(int level, const char* format, va_list valst);

tcpservice* tcp = 0;
void inst(int create)
{
	if (!create) {RELEASE(tcp)}
	else if (!tcp) {tcp = new tcpservice();}
	
};

void* threadstartserver(void *param) {
	((tcpservice*)((tagParam*)param)->param)->procstartserver(param); return 0;
}
void* threadrecv(void *param) {
	((tcpservice*)((tagParam*)param)->param)->procrecv(param); return 0;
}
void* threadfromto(void *param) {
	((tcpservice*)((tagParam*)param)->param)->procfromto(param); return 0;
}
void* threadtrans(void *param) {
	((tcpservice*)((tagParam*)param)->param)->proctrans(param); return 0;
}

tcpservice::tcpservice()
{
	reset();
}

tcpservice::~tcpservice()
{
	stop();
}

void tcpservice::reset()
{
	mpthreadfromto = 0;
	bfromtoworking = false;

	mpthreadstartserver = 0;
	bstartserverworking = false;
	
	maxsock = 0;
	
	for (int i = 0; i < MAXTHREADNUM; i++) {
		brecvworking[i] = false;
		mpthreadrecv[i] = 0;
		mapsock[i].clear();
	}
}

void tcpservice::stop()
{
	if (mpthreadstartserver && bstartserverworking) {
		bstartserverworking = false;
		pthread_join(mpthreadstartserver,0);
	}
	mpthreadstartserver = 0;
	
	if (mpthreadfromto && bfromtoworking) {
		bfromtoworking = false;
		pthread_join(mpthreadfromto,0);
	}
	mpthreadfromto = 0;
	maxsock = 0;
	
	for (int i = 0; i < MAXTHREADNUM; i++) {
		if (mpthreadrecv[i] && brecvworking[i]) {
			brecvworking[i] = false;
			pthread_join(mpthreadrecv[i],0);
		}
		mpthreadrecv[i] = 0;
		
		std::map<int, struct sockaddr_in>::iterator iter;  
		for(iter = mapsock[i].begin(); iter != mapsock[i].end(); iter++) {
			close(iter->first);
		}
		mapsock[i].clear();
    }
}

void tcpservice::startserver(int port, int listencount, int recvthreadcount)
{
	tagStartServerParam *pStartServerParam = new tagStartServerParam();
	struct sockaddr_in addr;
	int mode = 1;
	
	int sockfd= socket(AF_INET,SOCK_STREAM,0);
	if (0 > sockfd) {
		log(3,"%s[%d] socket error[%d]:%s", __FUNCTION__, __LINE__, errno, strerror(errno));
		goto error;
	}
	log("%s[%d] socket = %d ok", __FUNCTION__, __LINE__,sockfd);
	if (maxsock < sockfd) maxsock = sockfd;

	if (0 > ioctl(sockfd, FIONBIO, &mode)) {
		log(3,"%s[%d] ioctl error[%d]:%s", __FUNCTION__, __LINE__, errno, strerror(errno));
		goto error;
	}
	log("%s[%d] ioctl ok", __FUNCTION__, __LINE__);

	bzero(&addr, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (0 > bind(sockfd,(struct sockaddr *)(&addr),sizeof(struct sockaddr))) {
		log(3,"%s[%d] bind port=%d error[%d]:%s", __FUNCTION__, __LINE__, ntohs(addr.sin_port), errno, strerror(errno));
		goto error;
	}
	log("%s[%d] bind port=%d ok", __FUNCTION__, __LINE__, ntohs(addr.sin_port));
	
	if (0 > listen(sockfd,listencount)) {
		log(3,"%s[%d] listen %d error[%d]:%s", __FUNCTION__, __LINE__, port, errno, strerror(errno));
		goto error;
	}
	log("%s[%d] listen count=%d ok", __FUNCTION__, __LINE__, listencount);
	
	pStartServerParam->sockfd = sockfd;
	pStartServerParam->cpt = listencount / recvthreadcount;
	if (listencount % recvthreadcount) {
		pStartServerParam->cpt += 1;
	}
	pStartServerParam->param = this;
	pthread_create(&mpthreadstartserver, NULL, threadstartserver, (void*)pStartServerParam);
	return;
	
error:
	if (sockfd > 0) close(sockfd);
	delete pStartServerParam;
}

void tcpservice::procstartserver(void *param)
{
	log("%s[%d] enter", __FUNCTION__, __LINE__);
	tagStartServerParam *pStartServerParam = (tagStartServerParam *)param;
	tagStartServerParam stStartServerParam;
	memcpy(&stStartServerParam, pStartServerParam, sizeof(tagStartServerParam));
	if (pStartServerParam) delete pStartServerParam;
	
	int sockfd = stStartServerParam.sockfd;
	unsigned int cpt = (unsigned int)stStartServerParam.cpt;
	log("%s[%d] sockfd=%d,cpt=%d", __FUNCTION__, __LINE__, sockfd, cpt);
	
	for(int i = 0; i < MAXTHREADNUM; i++) {
		mpthreadrecv[i] = 0;
	}
	
	tagIndex stIndex;
	stIndex.param = this;
	
	struct sockaddr_in newaddr;
	socklen_t addrsize = sizeof(struct sockaddr);
	
	fd_set fdsetserver;
	struct timeval mtv;
	mtv.tv_usec = 0;
	int err = -1;
	bstartserverworking = true;
	while(bstartserverworking) {
		FD_ZERO(&fdsetserver);
		FD_SET(sockfd, &fdsetserver);
		mtv.tv_sec = 1;
		err = select(maxsock + 1, &fdsetserver, NULL, NULL, &mtv);
		if (0 > err) {
			log("%s[%d] select error[%d]:%s", __FUNCTION__,__LINE__,errno, strerror(errno));
			break;
		} else if (0 == err) {
			continue;
		}
		log("%s[%d] socket changed count %d!", __FUNCTION__,__LINE__, err);
		
		if (FD_ISSET(sockfd, &fdsetserver) > 0) {
			if (!bstartserverworking) break;
			bzero(&newaddr,sizeof(struct sockaddr_in));
			int newsock = accept(sockfd, (struct sockaddr*)&newaddr, &addrsize);
			if(newsock > 0) {
				log("%s[%d] client enter %s:%d", __FUNCTION__,__LINE__, inet_ntoa(newaddr.sin_addr), ntohs(newaddr.sin_port));
				if (maxsock < newsock) maxsock = newsock;
				for (int i = 0; i < MAXTHREADNUM; i++) {
					if (mapsock[i].size() < cpt) {
						mapsock[i][newsock] = newaddr;
						if (!mpthreadrecv[i]) {
							stIndex.i = i;
							pthread_create(&mpthreadrecv[i], NULL, threadrecv, (void*)&stIndex);
						}
						break;
					}
					if (!bstartserverworking) break;
				}	
			}
		}
		usleep(1000);
	}
	bstartserverworking = false;
	log("%s[%d] leave",__FUNCTION__, __LINE__);
}

void tcpservice::startrecv(int sockfd)
{
	tagIndex stIndex;
	stIndex.param = this;
	stIndex.i = 0; 
	if (!mpthreadrecv[stIndex.i]) {
		struct sockaddr_in addr;
		socklen_t addrsize = sizeof(struct sockaddr);
		bzero(&addr, sizeof(struct sockaddr_in));
		getsockname(sockfd, (struct sockaddr*)&addr, &addrsize);

		mapsock[stIndex.i][sockfd] = addr;
		pthread_create(&mpthreadrecv[stIndex.i], NULL, threadrecv, (void*)&stIndex);
	}
}

void tcpservice::procrecv(void* param)
{
	log("enter procrecv ok");
	tagIndex* pIndex = (tagIndex*)param;
	int index = pIndex->i;
	char buf[8192];
	
	std::map<int, struct sockaddr_in>::iterator iter;
	
	fd_set fdsetrecv;
	struct timeval mtv;
	mtv.tv_usec = 0;
	int err = -1;
	brecvworking[index] = true;
	while(brecvworking[index]) {
		FD_ZERO(&fdsetrecv);
		for(iter = mapsock[index].begin(); iter != mapsock[index].end(); iter++) {
			FD_SET(iter->first, &fdsetrecv);
		}
		
		mtv.tv_sec = 1;
		err = select(maxsock + 1, &fdsetrecv, 0, 0, &mtv);
		if (0 > err) {
			log("%s[%d] select error[%d]:%s", __FUNCTION__,__LINE__,errno, strerror(errno));
			break;
		} else if (0 == err) {
			continue;
		}
		

		for(iter = mapsock[index].begin(); iter != mapsock[index].end(); iter++) {
			if(FD_ISSET(iter->first, &fdsetrecv) > 0) {
				bzero(buf, sizeof(buf));
				if (recv(iter->first, buf, sizeof(buf), 0) > 0) {
					log("%s[%d] [%s:%d]:\"%s\"",__FUNCTION__,__LINE__,\
						inet_ntoa(iter->second.sin_addr), ntohs(iter->second.sin_port),buf);
					if (!brecvworking[index]) break;
					continue;
				}
				close(iter->first);
				log("%s[%d] client leave %s:%d",__FUNCTION__,__LINE__,\
					inet_ntoa(iter->second.sin_addr), ntohs(iter->second.sin_port));
				mapsock[index].erase(iter);
			}
			if (!brecvworking[index]) break;
		}
	}
	brecvworking[index] = false;
	log("%s[%d] leave procrecv ok",__FUNCTION__,__LINE__);
}

void tcpservice::startmakehole(const char* svrip, int svrport, std::vector<tagConfig>& vecConfig)
{
	struct sockaddr_in addr;
	socklen_t  addrsize = sizeof(struct sockaddr);
	
	std::vector<tagConfig>::iterator iter;
	for(iter = vecConfig.begin(); iter != vecConfig.end(); iter++) {
		iter->sockfrom = connecthost(svrip, svrport, true);
		if (0 > iter->sockfrom) break;
		
		bzero(&addr, sizeof(struct sockaddr_in));
		getsockname(iter->sockfrom, (struct sockaddr*)&addr, &addrsize);
		strcpy(iter->ipfrom, inet_ntoa(addr.sin_addr));
		iter->portfrom = ntohs(addr.sin_port);
		log("%s[%d] from %s:%d",__FUNCTION__,__LINE__, iter->ipfrom, iter->portfrom);
	}
	
	tagVecConfig stVecConfig;
	stVecConfig.param = this;
	stVecConfig.pVecConfig = &vecConfig;
	pthread_create(&mpthreadfromto,0,threadfromto,(void*)&stVecConfig);
}

void tcpservice::procfromto(void *param)
{
	log("%s[%d] enter",__FUNCTION__,__LINE__);
	tagVecConfig *pVecConfigIn = (tagVecConfig *)param;
	
	std::vector<tagConfig>* pVecConfig = pVecConfigIn->pVecConfig;
	std::vector<tagConfig>::iterator iter;
	
	pthread_t pthreadtrans;
	socklen_t addrsize = sizeof(struct sockaddr);
	struct sockaddr_in addr;
	
	fd_set fdsetfrom;
	struct timeval mtv;
	mtv.tv_usec = 0;
	int err = -1;
	bfromtoworking = true;
	while(bfromtoworking) {
		FD_ZERO(&fdsetfrom);
		for (iter = pVecConfig->begin(); iter != pVecConfig->end(); iter++) {
			FD_SET(iter->sockfrom,&fdsetfrom);
		}
		
		mtv.tv_sec = 1;
		err = select(maxsock + 1, &fdsetfrom, 0, 0, &mtv);
		if (0 > err) {
			log("%s[%d] select error[%d]:%s", __FUNCTION__,__LINE__,errno, strerror(errno));
			break;
		} else if (0 == err) {
			continue;
		}
		for (iter = pVecConfig->begin(); iter != pVecConfig->end(); iter++) {
			if (FD_ISSET(iter->sockfrom, &fdsetfrom) > 0) {
				bzero(&addr, sizeof(struct sockaddr_in));
				int newsock = accept(iter->sockfrom, (struct sockaddr*)&addr, &addrsize);
				if (newsock > 0) {
					if (maxsock < newsock) maxsock = newsock;
					iter->sockto = connecthost(iter->ipto, iter->portto, false);
					iter->param = this;
					pthread_create(&pthreadtrans, 0, threadtrans, (void*)&(*iter));
				}
			}
			if (!bfromtoworking) break;
		}
	}
	bfromtoworking = false;
	btransworking = false;
	for (iter = pVecConfig->begin(); iter != pVecConfig->end(); iter++) {
		if (iter->sockfrom > 0) close(iter->sockfrom);
		if (iter->sockto > 0) close(iter->sockto);
	}
	log("%s[%d] leave",__FUNCTION__,__LINE__);
}

void tcpservice::proctrans(void *param)
{
	log("%s[%d] enter",__FUNCTION__,__LINE__);
	tagConfig *pConfig = (tagConfig*)param;
	char RecvBuf[8192] = {0};
	int ret, nRecv;

	fd_set Fd_Read;
	struct timeval mtv;
	mtv.tv_usec = 0;
	btransworking = true;
	while(btransworking) {
		FD_ZERO(&Fd_Read);
		FD_SET(pConfig->sockfrom, &Fd_Read);
		FD_SET(pConfig->sockto, &Fd_Read);
	
		mtv.tv_sec = 1;
		if (0 >= (ret = select(maxsock + 1, &Fd_Read, 0, 0, &mtv)))
			goto error;
		if(FD_ISSET(pConfig->sockfrom, &Fd_Read))
		{
			if (0 >= (nRecv = recv(pConfig->sockfrom, RecvBuf, sizeof(RecvBuf), 0)))
				goto error;
			ret = datasend(pConfig->sockto, RecvBuf, nRecv);
			if(ret == 0 || ret != nRecv)
				goto error;
		}
		if(FD_ISSET(pConfig->sockto, &Fd_Read))
		{
			if (0 >= (nRecv = recv(pConfig->sockto, RecvBuf, sizeof(RecvBuf), 0)))
				goto error;
			ret = datasend(pConfig->sockfrom, RecvBuf, nRecv);
			if(ret == 0 || ret != nRecv)
				goto error;
		}
	}
	
error:
	btransworking = false;
	if (pConfig->sockfrom > 0) close(pConfig->sockfrom);
	if (pConfig->sockto > 0) close(pConfig->sockto);
	log("%s[%d] leave",__FUNCTION__,__LINE__);
}

int tcpservice::connecthost(const char* ip, int port,int reuseaddr)
{
	return connecthost(inet_addr(ip), port, reuseaddr);
}

int tcpservice::connecthost(unsigned long dwip, int port,int reuseaddr)
{
	in_addr inaddr;
	inaddr.s_addr = htonl(dwip);
	log("%s[%d] %s:%d", __FUNCTION__, __LINE__,inet_ntoa(inaddr), port);
	struct sockaddr_in addr;
	unsigned long mode = 1;
	
	int sockfd = socket(AF_INET,SOCK_STREAM,0);
	if (0 > sockfd) {
		log(3,"%s[%d] socket error[%d]:%s", __FUNCTION__, __LINE__, errno, strerror(errno));
		return 0;
	}
	log("%s[%d] socket=%d ok", __FUNCTION__, __LINE__,sockfd);
	if (maxsock < sockfd) maxsock = sockfd;
	
	if (0 > setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&reuseaddr,sizeof(reuseaddr))) {
		log(3,"%s[%d] setsockopt error[%d]:%s", __FUNCTION__, __LINE__, errno, strerror(errno));
		goto error;
	}
	log("%s[%d] setsockopt ok", __FUNCTION__, __LINE__);

	if (0 > ioctl(sockfd, FIONBIO, &mode)) {
		log(3,"%s[%d] ioctl error[%d]:%s", __FUNCTION__, __LINE__, errno, strerror(errno));
		goto error;
	}
	log("%s[%d] ioctl ok", __FUNCTION__, __LINE__);

	bzero(&addr, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr = inaddr;
	if (0 > connect(sockfd,(struct sockaddr*)&addr,sizeof(struct sockaddr_in))) {
		log(3,"%s[%d] connect %s:%d error[%d]:%s", __FUNCTION__, __LINE__, \
			inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), errno, strerror(errno));
		goto error;
	}
	log("%s[%d] connect ok", __FUNCTION__, __LINE__);
	return sockfd;
	
error:
	if (sockfd > 0) close(sockfd);
	return -1;
}

int tcpservice::datasend(int sockfd, const char* buf, int bufsize)
{
	int bufsizeleft = bufsize;
	int bufsizesend = 0;
	int ret;
	//set socket to blocking mode
	int mode = 0;
	if (0 > ioctl(sockfd, FIONBIO, &mode)) {
		log(3,"%s[%d] ioctlsocket error[%d]:%s", __FUNCTION__, __LINE__, errno, strerror(errno));
		return 0;
	}
	while(bufsizeleft > 0) {
		if (0 >= (ret = send(sockfd, buf + bufsizesend, bufsizeleft, 0)))
			break;
		bufsizesend += ret;
		bufsizeleft -= ret;
	}
	return bufsizesend;
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
