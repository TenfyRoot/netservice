#include "netservice.h"
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <algorithm>
#include <functional>

namespace netservice {

#define RELEASE(p) {if (p) {delete p; p = 0;}}

void log(const char* format, ...);
void log(int level, const char* format, ...);
void loglevel(int level, const char* format, va_list valst);

#include <stdint.h>
#ifdef __x86_64__
	#define OFFSET(structure, member) ((int64_t)&((structure*)0)->member)
#elif __i386__
	#define OFFSET(structure, member) ((int32_t)&((structure*)0)->member)
#endif

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
	mListenEpollfd = mHoleEpollfd = mTransEpollfd = -1;
	mvecListenEpEvent.resize(100);
	mvecHoleEpEvent.resize(100);
	mvecTransEpEvent.resize(100);
	
	mpthreadfromto = 0;
	bfromtoworking = false;

	mpthreadstartserver = 0;
	bstartserverworking = false;
	
	for (int i = 0; i < MAXTHREADNUM; i++) {
		mRecvEpollfd[i] = -1;
		mvecRecvEpEvent[i].resize(100);
		mbrecvworking[i] = false;
		mpthreadrecv[i] = 0;
	}
}

void tcpservice::stop()
{
	if (0 < mListenEpollfd) close(mListenEpollfd);
	if (0 < mHoleEpollfd) close(mHoleEpollfd);
	if (0 < mTransEpollfd) close(mTransEpollfd);
	std::map<int, std::vector<int> >::iterator itermap;
	std::vector<int>::iterator itervec;
	for (itermap = mmapEpfd.begin(); itermap != mmapEpfd.end(); ++itermap) {
		for (itervec = itermap->second.begin(); itervec != itermap->second.end(); ++itervec) {
			if (*itervec) close(*itervec);
		}
		itermap->second.clear();
	}
	mmapEpfd.clear();
	
	mvecListenEpEvent.clear();
	mvecHoleEpEvent.clear();
	mvecTransEpEvent.clear();	
	
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
	
	for (int i = 0; i < MAXTHREADNUM; i++) {
		if (mRecvEpollfd[i]) close(mRecvEpollfd[i]);
		mvecRecvEpEvent[i].clear();
		
		if (mpthreadrecv[i] && mbrecvworking[i]) {
			mbrecvworking[i] = false;
			pthread_join(mpthreadrecv[i],0);
		}
		mpthreadrecv[i] = 0;
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
		log(3,"%s[%d] bind %s:%d error[%d]:%s", __FUNCTION__, __LINE__, \
			inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), errno, strerror(errno));
		goto error;
	}
	log("%s[%d] bind port=%d ok", __FUNCTION__, __LINE__, ntohs(addr.sin_port));
	
	if (0 > listen(sockfd,listencount)) {
		log(3,"%s[%d] listen %d error[%d]:%s", __FUNCTION__, __LINE__, port, errno, strerror(errno));
		goto error;
	}
	log("%s[%d] listen count=%d ok", __FUNCTION__, __LINE__, listencount);
	
	addfd(mListenEpollfd, sockfd);

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
	
	unsigned int cpt = (unsigned int)stStartServerParam.cpt;
	std::map<int, std::vector<int> >::iterator itermap;

	int socklisten;
	struct sockaddr_in addr;
	socklen_t socklen = sizeof(struct sockaddr);

	bstartserverworking = true;
	while(bstartserverworking) {
		int ready = epoll_wait(mListenEpollfd, &*mvecListenEpEvent.begin(), mvecListenEpEvent.size(), 100);
		if (0 > ready) {
			log("%s[%d] epoll_wait error[%d]:%s", __FUNCTION__,__LINE__,errno, strerror(errno));
			break;
		} else if (0 == ready) {
			continue;
		}
		if (ready == (int)mvecListenEpEvent.size()) mvecListenEpEvent.resize(ready * 2);

		for (int i = 0; i < ready && bstartserverworking; ++ i) {
			socklisten = mvecListenEpEvent[i].data.fd;
			bzero(&addr,sizeof(struct sockaddr_in));
			int recvsock = accept(socklisten, (struct sockaddr*)&addr, &socklen);
			if(recvsock > 0) {
				log("%s[%d] client enter %s:%d", __FUNCTION__,__LINE__, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
				for (int i = 0; i < MAXTHREADNUM && bstartserverworking; i++) {
					itermap = mmapEpfd.find(mRecvEpollfd[i]);
					if (itermap == mmapEpfd.end() || itermap->second.size() < cpt) {
						addfd(mRecvEpollfd[i], recvsock);
						if (!mbrecvworking[i]) {
							tagIndex *pIndex = new tagIndex();
							pIndex->param = this;
							pIndex->i = i;
							pthread_create(&mpthreadrecv[i], NULL, threadrecv, (void*)pIndex);
						}
						break;
					}
				}
			}
		}
	}
	bstartserverworking = false;
	log("%s[%d] leave",__FUNCTION__, __LINE__);
}

void tcpservice::procrecv(void* param)
{
	tagIndex* pIndex = (tagIndex*)param;
	int index = pIndex->i;
	if (pIndex) delete pIndex;
	
	log("%s[%d] enter procrecv[%d] ok", __FUNCTION__,__LINE__,index);
	
	int recvsock;
	int nRecv;
	char revbuf[8192];
	struct sockaddr_in addr;
	socklen_t socklen = sizeof(struct sockaddr);
	
	std::map<int, struct sockaddr_in>::iterator iter;

	mbrecvworking[index] = true;
	while(mbrecvworking[index]) {
		int ready = epoll_wait(mRecvEpollfd[index], &*mvecRecvEpEvent[index].begin(), mvecRecvEpEvent[index].size(), 100);
		if (0 > ready) {
			log("%s[%d] epoll_wait error[%d]:%s", __FUNCTION__,__LINE__,errno, strerror(errno));
			break;
		} else if (0 == ready) {
			continue;
		}
		if (ready == (int)mvecRecvEpEvent[index].size()) mvecRecvEpEvent[index].resize(ready * 2);
		
		for (int i = 0; i < ready && mbrecvworking[index]; ++ i) {
			recvsock = mvecRecvEpEvent[index][i].data.fd;
			bzero(&addr, sizeof(struct sockaddr_in));
			getsockname(recvsock, (struct sockaddr *)(&addr), &socklen);
			
			bzero(revbuf, sizeof(revbuf));
			nRecv = recv(recvsock, revbuf, sizeof(revbuf), 0);
			if (0 < nRecv) {
				log("%s[%d] [%s:%d]:\"%s\"",__FUNCTION__,__LINE__, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), revbuf);
			} else {
				log("%s[%d] client leave %s:%d",__FUNCTION__,__LINE__, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
				delfd(mRecvEpollfd[index], recvsock);
			}
		}
	}
	mbrecvworking[index] = false;
	log("%s[%d] leave procrecv[%d] ok",__FUNCTION__,__LINE__,index);
}

void tcpservice::startrecv(int sockfd)
{
	tagIndex stIndex;
	stIndex.param = this;
	stIndex.i = 0;
	if (!mpthreadrecv[stIndex.i]) {
		pthread_create(&mpthreadrecv[stIndex.i], NULL, threadrecv, (void*)&stIndex);
	}
}

void tcpservice::startservertrans(const char* svrip, int svrport, std::vector<tagConfig>& vecConfig)
{
	int reuseaddr = true;
	int mode = 1;
	int sockfd;
	struct sockaddr_in addr;
	socklen_t socklen = sizeof(struct sockaddr);
	
	std::vector<tagConfig>::iterator iter;
	for(iter = vecConfig.begin(); iter != vecConfig.end(); iter++) {
		sockfd = connecthost(svrip, svrport, reuseaddr);
		if (0 > sockfd) continue;
		
		bzero(&addr, sizeof(struct sockaddr_in));
		getsockname(sockfd, (struct sockaddr *)(&addr), &socklen);
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		
		iter->sockhole = socket(AF_INET, SOCK_STREAM, 0);
		if (0 > iter->sockhole) {
			log(3,"%s[%d] socket error[%d]:%s", __FUNCTION__, __LINE__, errno, strerror(errno));
			continue;
		}

		if (0 > setsockopt(iter->sockhole, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr))) {
			log(3,"%s[%d] setsockopt error[%d]:%s", __FUNCTION__, __LINE__, errno, strerror(errno));
			goto error;
		}

		if (0 > bind(iter->sockhole, (struct sockaddr *)(&addr), sizeof(struct sockaddr))) {
			log(3,"%s[%d] bind %s:%d error[%d]:%s", __FUNCTION__, __LINE__, \
				inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), errno, strerror(errno));
			goto error;
		}
	
		if (0 > listen(iter->sockhole, 1000)) {
			log(3,"%s[%d] listen %d error[%d]:%s", __FUNCTION__, __LINE__, ntohs(addr.sin_port), errno, strerror(errno));
			goto error;
		}
		
		if (0 > ioctl(iter->sockhole, FIONBIO, &mode)) {
			log(3,"%s[%d] ioctl error[%d]:%s", __FUNCTION__, __LINE__, errno, strerror(errno));
			goto error;
		}

		strcpy(iter->ipfrom, inet_ntoa(addr.sin_addr));
		iter->portfrom = ntohs(addr.sin_port);
		addfd(mHoleEpollfd, iter->sockhole);
		
		log("%s[%d] port %d -> %s:%d",__FUNCTION__,__LINE__, iter->portfrom, iter->ipto, iter->portto);
		continue;
error:
		close(iter->sockhole);
		continue;
	}
	
	if (mmapEpfd.find(mHoleEpollfd) == mmapEpfd.end() ||
		mmapEpfd[mHoleEpollfd].size() <= 0) {
		return;	
	}
	
	tagVecConfig* pVecConfig = new tagVecConfig();
	pVecConfig->param = this;
	pVecConfig->pVecConfig = &vecConfig;
	pthread_create(&mpthreadfromto,0,threadfromto,(void*)pVecConfig);
}

struct Finder
{
	int sockhole;
 	Finder(int fd) : sockhole(fd) {}
	bool operator()(tagConfig& in) {
		return in.sockhole == sockhole;
	}
};

void tcpservice::procfromto(void *param)
{
	tagVecConfig *pVecConfigIn = (tagVecConfig *)param;
	log("%s[%d] enter",__FUNCTION__,__LINE__);
	std::vector<tagConfig>* pVecConfig = pVecConfigIn->pVecConfig;
	std::vector<tagConfig>::iterator iter;
	mmapfdflag.clear();
	
	pthread_t pthreadtrans;
	int socklisten;
	struct sockaddr_in addr;
	socklen_t socklen = sizeof(struct sockaddr);
	
	bfromtoworking = true;
	while(bfromtoworking) {
		int ready = epoll_wait(mHoleEpollfd, &*mvecHoleEpEvent.begin(), mvecHoleEpEvent.size(), 100);
		if (0 > ready) {
			log("%s[%d] epoll_wait error[%d]:%s", __FUNCTION__,__LINE__,errno, strerror(errno));
			break;
		} else if (0 == ready) {
			continue;
		}
		if (ready == (int)mvecHoleEpEvent.size()) mvecHoleEpEvent.resize(ready * 2);
		
		for (int i = 0; i < ready && bfromtoworking; ++ i) {
			socklisten = mvecHoleEpEvent[i].data.fd;
			iter = std::find_if(pVecConfig->begin(), pVecConfig->end(), Finder(socklisten));
			if (iter == pVecConfig->end()) {
				log("%s[%d] config not found socket %d", __FUNCTION__,__LINE__, socklisten);
				close(accept(socklisten, (struct sockaddr*)&addr, &socklen));
				delfd(mHoleEpollfd, socklisten);
				continue;
			} 
			bzero(&addr,sizeof(struct sockaddr_in));
			iter->sockrecv = accept(socklisten, (struct sockaddr*)&addr, &socklen);
			if(iter->sockrecv > 0) {
				log("%s[%d] app enter %s:%d", __FUNCTION__,__LINE__, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
				if (0 > iter->sockto) {
					iter->sockto = connecthost(iter->ipto, iter->portto, false);
					if (0 > iter->sockto) {
						delfd(mHoleEpollfd, socklisten);
						close(iter->sockrecv);
					}
				}
				if (iter->sockto) {
					mmapfdflag[iter->sockrecv] = iter->sockto;
					mmapfdflag[iter->sockto] = iter->sockrecv;
					addfd(mTransEpollfd, iter->sockrecv);
					addfd(mTransEpollfd, iter->sockto);
					tagParam *pParam = new tagParam();
					pParam->param = this;
					pthread_create(&pthreadtrans, 0, threadtrans, (void*)pParam);
				} 
			}
		}
	}
	bfromtoworking = false;
	btransworking = false;
	for (iter = pVecConfig->begin(); iter != pVecConfig->end(); iter++) {
		delfd(mHoleEpollfd, iter->sockhole);
		delfd(mTransEpollfd,iter->sockto);
		delfd(mTransEpollfd,iter->sockrecv);
	}
	if (pVecConfigIn) delete pVecConfigIn;
	log("%s[%d] leave",__FUNCTION__,__LINE__);
}

void tcpservice::proctrans(void *param)
{
	tagParam *pParam = (tagParam *)param;
	if (pParam) delete pParam;
	
	log("%s[%d] enter",__FUNCTION__,__LINE__);
	char recvbuf[8192] = {0};
	int ret, nRecv;
	
	int sockfd;
	btransworking = true;
	while(btransworking) {
		int ready = epoll_wait(mTransEpollfd, &*mvecTransEpEvent.begin(), mvecTransEpEvent.size(), 100);
		if (0 > ready) {
			log("%s[%d] epoll_wait error[%d]:%s", __FUNCTION__,__LINE__,errno, strerror(errno));
			goto error;
		} else if (0 == ready) {
			continue;
		}
		if (ready == (int)mvecTransEpEvent.size()) mvecTransEpEvent.resize(ready * 2);
		
		for (int i = 0; i < ready && btransworking; ++ i) {
			bzero(recvbuf, sizeof(recvbuf));
			sockfd = mvecTransEpEvent[i].data.fd;
			nRecv = recv(sockfd, recvbuf, sizeof(recvbuf), 0);
			if (0 >= nRecv) {
				if (errno > 0 && errno != EAGAIN) 
					log("%s[%d] recv=%d error[%d]:%s", __FUNCTION__,__LINE__, nRecv, errno, strerror(errno));
				goto error;
			}
			ret = datasend(mmapfdflag[sockfd], recvbuf, nRecv);
			if (ret == 0 || ret != nRecv) break;
		}
	}

error:
	delfd(mTransEpollfd, sockfd);
	delfd(mTransEpollfd, mmapfdflag[sockfd]);
	log("%s[%d] leave",__FUNCTION__,__LINE__);
}

int tcpservice::datasend(int sockfd, const char* buf, int bufsize)
{
	int bufsizeleft = bufsize;
	int bufsizesend = 0;
	int ret;
	//set socket to blocking mode
	int mode = 0;
	if (0 > ioctl(sockfd, FIONBIO, &mode)) {
		log(3,"%s[%d] ioctlsocket error[%d]:%s", __FUNCTION__,__LINE__, errno, strerror(errno));
		return 0;
	}

	while(bufsizeleft > 0) {
		ret = send(sockfd, buf + bufsizesend, bufsizeleft, 0);
		if (0 >= ret) {
			log(3,"%s[%d] sockfd=%d, left=%d, ret=%d error[%d]:%s", __FUNCTION__,__LINE__, \
				sockfd, bufsizeleft, ret, errno, strerror(errno));
			break;
		} else {
			log("%s[%d] send size=%d", __FUNCTION__,__LINE__, ret, errno, strerror(errno));
		}
		bufsizesend += ret;
		bufsizeleft -= ret;
	}
	return bufsizesend;
}

int tcpservice::connecthost(const char* ip, int port,int reuseaddr)
{
	return connecthost(inet_addr(ip), port, reuseaddr);
}

int tcpservice::connecthost(unsigned long dwip, int port,int reuseaddr)
{
	struct sockaddr_in addr;
	
	int sockfd = socket(AF_INET,SOCK_STREAM,0);
	if (0 > sockfd) {
		log(3,"%s[%d] socket error[%d]:%s", __FUNCTION__, __LINE__, errno, strerror(errno));
		return 0;
	}
	log("%s[%d] socket=%d ok", __FUNCTION__, __LINE__,sockfd);
	
	if (0 > setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr))) {
		log(3,"%s[%d] setsockopt error[%d]:%s", __FUNCTION__, __LINE__, errno, strerror(errno));
		goto error;
	}
	log("%s[%d] setsockopt ok", __FUNCTION__, __LINE__);

	bzero(&addr, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = dwip;
	if (0 > connect(sockfd, (struct sockaddr *)(&addr), sizeof(struct sockaddr))) {
		log(3,"%s[%d] connect %s:%d error[%d]:%s", __FUNCTION__, __LINE__, \
			inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), errno, strerror(errno));
		goto error;
	}
	log("%s[%d] connect %s:%d ok", __FUNCTION__, __LINE__, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
	return sockfd;
	
error:
	if (sockfd > 0) close(sockfd);
	return -1;
}

void tcpservice::addfd(int& epfd, int opfd)
{	
	if (0 > opfd) return;
	if (0 > epfd) {
		epfd = epoll_create1(0);
	}
	epoll_event event;
	event.data.fd = opfd;
	event.events  = EPOLLET | EPOLLIN;
	epoll_ctl(epfd, EPOLL_CTL_ADD, opfd, &event);
	mmapEpfd[epfd].push_back(opfd);
}

void tcpservice::delfd(int& epfd, int opfd)
{
	if (0 > epfd || 0 > opfd) return;
	if (mmapEpfd.find(epfd) == mmapEpfd.end()) return;
	
	epoll_event event;
	epoll_ctl(epfd, EPOLL_CTL_DEL, opfd, &event);
	
	std::vector<int>::iterator result = std::find(mmapEpfd[epfd].begin(), mmapEpfd[epfd].end(), opfd);
	if (result != mmapEpfd[epfd].end()) {
		mmapEpfd[epfd].erase(result);
		
		struct sockaddr_in addr;
		socklen_t socklen = sizeof(struct sockaddr);
		bzero(&addr, sizeof(struct sockaddr_in));
		getsockname(opfd, (struct sockaddr *)(&addr), &socklen);
		log("%s[%d] delfd %s:%d",__FUNCTION__,__LINE__, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
		close(opfd);
	}
	if (mmapEpfd[epfd].size() == 0) {
		close(epfd);
	}
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
