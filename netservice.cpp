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
#include <time.h>

namespace netservice {

#define release(p) {if (p) {delete p; p = 0;}}

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
	if (!create) release(tcp);
	if (create && !tcp) {tcp = new tcpservice();}
	
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
	
	for (int i = 0; i < MAXTHREADNUM; i++) {
		mRecvEpollfd[i] = -1;
		mbrecvworking[i] = false;
		mpthreadrecv[i] = 0;
	}
	
	mListenEpollfd = mHoleEpollfd = -1;
}

void tcpservice::stop()
{
	if (mpthreadstartserver && bstartserverworking) {
		bstartserverworking = false;
		pthread_join(mpthreadstartserver,0);
	}
	
	if (mpthreadfromto && bfromtoworking) {
		bfromtoworking = false;
		pthread_join(mpthreadfromto,0);
	}
	
	for (int i = 0; i < MAXTHREADNUM; i++) {
		clrfd(mRecvEpollfd[i]);
		
		if (mpthreadrecv[i] && mbrecvworking[i]) {
			mbrecvworking[i] = false;
			pthread_join(mpthreadrecv[i],0);
		}
    }
    
    clrfd(mListenEpollfd);
	clrfd(mHoleEpollfd);
}

void tcpservice::startserver(int port, callbackrecv callback, int listencount, int recvthreadcount)
{
	tagStartServerParam *pStartServerParam = new tagStartServerParam();
	struct sockaddr_in addr;

	int sockfd= socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (0 > sockfd) {
		log(3,"%s[%d] socket error[%d]:%s", __FUNCTION__, __LINE__, errno, strerror(errno));
		goto error;
	}
	log("%s[%d] socket = %d ok", __FUNCTION__, __LINE__,sockfd);

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
	
	if (callback) {
		mmapRecvFunc[sockfd] = callback;
	}
	
	setsockbuf(sockfd);
	setnonblock(sockfd);
	addfd(mListenEpollfd, sockfd);
	
	pStartServerParam->cpt = listencount / recvthreadcount;
	if (listencount % recvthreadcount) {
		pStartServerParam->cpt += 1;
	}
	pStartServerParam->param = this;
	pthread_create(&mpthreadstartserver, NULL, threadstartserver, (void*)pStartServerParam);
	return;
	
error:
	if (0 < sockfd) close(sockfd); sockfd = -1;
	release(pStartServerParam);
}

void tcpservice::procstartserver(void *param)
{
	log("%s[%d] enter", __FUNCTION__, __LINE__);
	tagStartServerParam *pStartServerParam = (tagStartServerParam *)param;
	tagStartServerParam stStartServerParam;
	memcpy(&stStartServerParam, pStartServerParam, sizeof(tagStartServerParam));
	release(pStartServerParam);
	
	unsigned int cpt = (unsigned int)stStartServerParam.cpt;
	std::map<int, std::vector<int> >::iterator itermap;

	int socklisten;
	struct sockaddr_in addr;
	socklen_t socklen = sizeof(struct sockaddr);
	
	std::vector<epoll_event> vecEpEvent; vecEpEvent.resize(100);

	int& epfd = mListenEpollfd;
	bstartserverworking = true;
	while(bstartserverworking) {
		if (0 > epfd) break;
		int ready = epoll_wait(epfd, &*vecEpEvent.begin(), vecEpEvent.size(), 100);
		if (0 > ready) {
			if (errno == EINTR) continue;
			log("%s[%d] epoll_wait error[%d]:%s", __FUNCTION__,__LINE__,errno, strerror(errno));
			break;
		} else if (0 == ready) {
			continue;
		}
		if (ready == (int)vecEpEvent.size()) vecEpEvent.resize(ready * 2);

		for (int i = 0; i < ready && bstartserverworking; ++ i) {
			socklisten = vecEpEvent[i].data.fd;
			bzero(&addr,sizeof(struct sockaddr_in));
			int sockrecv = accept(socklisten, (struct sockaddr*)&addr, &socklen);
			if(sockrecv > 0) {
				mmapListenfdClientfds[socklisten].push_back(sockrecv);
				log("%s[%d] client enter %s:%d", __FUNCTION__,__LINE__, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
				for (int i = 0; i < MAXTHREADNUM && bstartserverworking; i++) {
					itermap = mmapEpfd.find(mRecvEpollfd[i]);
					if (itermap == mmapEpfd.end() || itermap->second.size() < cpt) {
						setsockbuf(sockrecv);
						setnonblock(sockrecv);
						addfd(mRecvEpollfd[i], sockrecv);
						if (!mbrecvworking[i]) {
							tagIndex *pIndex = new tagIndex();
							pIndex->param = this;
							pIndex->i = i;
							if (mmapRecvFunc.find(socklisten) != mmapRecvFunc.end()) {
								pIndex->data = (void*)mmapRecvFunc[socklisten];
							}
							pthread_create(&mpthreadrecv[i], NULL, threadrecv, (void*)pIndex);
						}
						break;
					}
				}
			}
		}
	}
	bstartserverworking = false;
	mpthreadstartserver = 0;
	log("%s[%d] leave",__FUNCTION__, __LINE__);
}

void tcpservice::procrecv(void* param)
{
	tagIndex* pIndex = (tagIndex*)param;
	int index = pIndex->i;
	callbackrecv callbackrecvfunc = (callbackrecv)pIndex->data;
	release(pIndex);
	
	log("%s[%d] enter procrecv[%d] ok", __FUNCTION__,__LINE__,index);
	
	int sockrecv;
	int nRecv;
	char revbuf[8192];
	struct sockaddr_in addr;
	socklen_t socklen = sizeof(struct sockaddr);
	
	std::map<int, struct sockaddr_in>::iterator iter;
	std::vector<epoll_event> vecEpEvent; vecEpEvent.resize(100);

	int& epfd = mRecvEpollfd[index];
	mbrecvworking[index] = true;
	while(mbrecvworking[index]) {
		if (0 > epfd) break;
		int ready = epoll_wait(epfd, &*vecEpEvent.begin(), vecEpEvent.size(), 100);
		if (0 > ready) {
			if (errno == EINTR) continue;
			log("%s[%d] [i%d] epoll_wait error[%d]:%s", __FUNCTION__,__LINE__, index, errno, strerror(errno));
			break;
		} else if (0 == ready) {
			continue;
		}
		if (ready == (int)vecEpEvent.size())vecEpEvent.resize(ready * 2);
		
		for (int i = 0; i < ready && mbrecvworking[index]; ++ i) {
			sockrecv = vecEpEvent[i].data.fd;
			bzero(&addr, sizeof(struct sockaddr_in));
			getsockname(sockrecv, (struct sockaddr *)(&addr), &socklen);
			
			bzero(revbuf, sizeof(revbuf));
			nRecv = recv(sockrecv, revbuf, sizeof(revbuf), 0);
			if (0 < nRecv) {
				if (callbackrecvfunc) {
					callbackrecvfunc(sockrecv, revbuf);
				} else {
					log("%s[%d] [%s:%d]:\"%s\"",__FUNCTION__,__LINE__, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), revbuf);
				}
			} else {
				log("%s[%d] client leave %s:%d",__FUNCTION__,__LINE__, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
				delfd(mRecvEpollfd[index], sockrecv);
			}
		}
	}
	mbrecvworking[index] = false;
	mpthreadrecv[index] = 0;
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
	int sockfd;
	struct sockaddr_in addr;
	socklen_t socklen = sizeof(struct sockaddr);
	
	std::vector<tagConfig>::iterator iter;
	for(iter = vecConfig.begin(); iter != vecConfig.end(); iter++) {
		sockfd = connecthost(svrip, svrport, reuseaddr);
		if (0 > sockfd) continue;

		bzero(&addr, sizeof(struct sockaddr_in));getsockname(sockfd, (struct sockaddr *)(&addr), &socklen);
		
		iter->sockhole = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
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
		
		
		
		setsockbuf(iter->sockhole);
		setnonblock(iter->sockhole);

		strcpy(iter->ipfrom, inet_ntoa(addr.sin_addr));
		iter->portfrom = ntohs(addr.sin_port);
		addfd(mHoleEpollfd, iter->sockhole);
		
		log("%s[%d] from %s:%d to %s:%d",__FUNCTION__,__LINE__, iter->ipfrom, iter->portfrom, iter->ipto, iter->portto);
		continue;
error:
		if (0 < iter->sockhole) close(iter->sockhole); iter->sockhole = -1;
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
	std::vector<tagConfig>* pVecConfig = pVecConfigIn->pVecConfig;
	std::vector<tagConfig>::iterator iter;
	
	log("%s[%d] enter",__FUNCTION__,__LINE__);

	int socklisten;
	struct sockaddr_in addr;
	socklen_t socklen = sizeof(struct sockaddr);
	std::vector<epoll_event> vecEpEvent; vecEpEvent.resize(100);
	
	int& epfd = mHoleEpollfd;
	bfromtoworking = true;
	while(bfromtoworking) {
		if (0 > epfd) break;
		int ready = epoll_wait(epfd, &*vecEpEvent.begin(), vecEpEvent.size(), 100);
		if (0 > ready) {
			if (errno == EINTR) continue;
			log("%s[%d] epoll_wait error[%d]:%s", __FUNCTION__,__LINE__,errno, strerror(errno));
			break;
		} else if (0 == ready) {
			continue;
		}
		if (ready == (int)vecEpEvent.size()) vecEpEvent.resize(ready * 2);
		
		for (int i = 0; i < ready && bfromtoworking; ++ i) {
			socklisten = vecEpEvent[i].data.fd;
			iter = std::find_if(pVecConfig->begin(), pVecConfig->end(), Finder(socklisten));
			if (iter == pVecConfig->end()) {
				log("%s[%d] config not found socket %d", __FUNCTION__,__LINE__, socklisten);
				close(accept(socklisten, (struct sockaddr*)&addr, &socklen));
				delfd(mHoleEpollfd, socklisten);
				continue;
			}
			
			bzero(&addr,sizeof(struct sockaddr_in));
			int sockrecv = accept(socklisten, (struct sockaddr*)&addr, &socklen);
			if(sockrecv > 0) {
				log("%s[%d] app s%d enter %s:%d", __FUNCTION__,__LINE__, sockrecv, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
				int sockto = connecthost(iter->ipto, iter->portto, false);
				if (0 >= sockto) {
					close(sockrecv);
				} else {
					setsockbuf(sockrecv);
					setnonblock(sockrecv);
					setsockbuf(sockto);
					setnonblock(sockto);
		
					mmutex.lock();
					mmapTransParam[sockto] = tagTransParam(sockrecv,sockto);
					mmutex.unlock();
					
					tagIndex *pIndex = new tagIndex();
					pIndex->param = this;
					pIndex->i = sockto;
					pthread_create(&(mmapTransParam[sockto].pthreadtrans), 0, threadtrans, (void*)pIndex);
				} 
			}
		}
	}

	bfromtoworking = false;
	mpthreadfromto = 0;
	
	for (iter = pVecConfig->begin(); iter != pVecConfig->end(); iter++) {
		delfd(mHoleEpollfd, iter->sockhole);
	}
	release(pVecConfigIn);
	
	std::map<int, tagTransParam>::iterator itermap;
	for (itermap = mmapTransParam.begin(); itermap != mmapTransParam.end(); ++itermap) {
		if (itermap->second.pthreadtrans && itermap->second.btransworking) {
			itermap->second.btransworking = false;
			pthread_join(itermap->second.pthreadtrans,0);
		}
	}
	mmapTransParam.clear();
	
	log("%s[%d] leave",__FUNCTION__,__LINE__);
}

void tcpservice::proctrans(void *param)
{
	tagIndex* pIndex = (tagIndex*)param;
	int index = pIndex->i;
	release(pIndex);
	
	log("%s[%d] *************************************************** [%03d] enter",__FUNCTION__,__LINE__, index);
	
	int sockfd;
	char recvbuf[1024 * 100] = {0};
	int ret, nRecv;

	std::map<int, tagTransParam>::iterator itermap = mmapTransParam.find(index);
	if (itermap == mmapTransParam.end()) {
		log("%s[%d] *************************************************** [%03d] leave",__FUNCTION__,__LINE__, index);
		return;
	}
	mmutex.lock();
	int& sockrecv = itermap->second.sockrecv;
	int& sockto = itermap->second.sockto;
	bool& btransworking = itermap->second.btransworking;
	pthread_t& pthreadtrans = itermap->second.pthreadtrans;
	mmutex.unlock();
	
	int epfd = -1;
	addfd(epfd, sockrecv);
	addfd(epfd, sockto);
	
	std::vector<epoll_event> vecEpEvent; vecEpEvent.resize(100); 
	
	btransworking = true;
	while(btransworking) {
		if (0 > epfd) break;
		int ready = epoll_wait(epfd, &*vecEpEvent.begin(), vecEpEvent.size(), 100);
		if (0 > ready) {
			if (errno == EINTR) continue;
			log("%s[%d] [%03d] epoll_wait error[%d]:%s", __FUNCTION__,__LINE__, index, errno, strerror(errno));
			goto error;
		} else if (0 == ready) {
			continue;
		}
		if (ready == (int)vecEpEvent.size()) vecEpEvent.resize(ready * 2);
		
		for (int i = 0; i < ready && btransworking; ++ i) {
			bzero(recvbuf, sizeof(recvbuf));
			sockfd = vecEpEvent[i].data.fd;

			nRecv = read(sockfd, recvbuf, sizeof(recvbuf));
			if (0 >= nRecv) {
				if (errno > 0 && errno != EAGAIN) 
					log("%s[%d] [%03d] recv=%d error[%d]:%s", __FUNCTION__,__LINE__, index, nRecv, errno, strerror(errno));
				goto error;
			}
			ret = datasend((sockfd == sockto ? sockrecv : sockto), recvbuf, nRecv);
			if (ret == 0 || ret != nRecv)
				goto error;
		}
	}

error:
	btransworking = false;
	pthreadtrans = 0;
	clrfd(epfd);
	
	mmutex.lock();
	itermap = mmapTransParam.find(index);
	if (itermap != mmapTransParam.end()) {
		mmapTransParam.erase(itermap);
	}
	mmutex.unlock();
	log("%s[%d] *************************************************** [%03d] leave",__FUNCTION__,__LINE__, index);
}

int tcpservice::datasend(int sockfd, const char* buf, int bufsize)
{
	int bufsizeleft = bufsize;
	int bufsizesend = 0;
	int ret;

	while(bufsizeleft > 0) {
		ret = write(sockfd, buf + bufsizesend, bufsizeleft);
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
	
	int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (0 > sockfd) {
		log(3,"%s[%d] socket error[%d]:%s", __FUNCTION__, __LINE__, errno, strerror(errno));
		return 0;
	}
	
	if (0 > setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr))) {
		log(3,"%s[%d] setsockopt error[%d]:%s", __FUNCTION__, __LINE__, errno, strerror(errno));
		goto error;
	}

	bzero(&addr, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = dwip;
	if (0 > connect(sockfd, (struct sockaddr *)(&addr), sizeof(struct sockaddr))) {
		log(3,"%s[%d] connect %s:%d error[%d]:%s", __FUNCTION__, __LINE__, \
			inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), errno, strerror(errno));
		goto error;
	}
	return sockfd;
	
error:
	if (0 < sockfd) close(sockfd); sockfd = -1;
	return -1;
}

void tcpservice::setsockbuf(int sockfd)
{
	if (0 > sockfd) return;
	int nRecvBuf=128*1024;
	if (0 > setsockopt(sockfd,SOL_SOCKET,SO_RCVBUF,(const char*)&nRecvBuf,sizeof(int))) {
		log(3,"%s[%d] setsockopt recvbuf error[%d]:%s", __FUNCTION__, __LINE__, errno, strerror(errno));
	}
	int nSendBuf=128*1024;
	if (0 > setsockopt(sockfd,SOL_SOCKET,SO_SNDBUF,(const char*)&nSendBuf,sizeof(int))) {
		log(3,"%s[%d] setsockopt sendbuf error[%d]:%s", __FUNCTION__, __LINE__, errno, strerror(errno));
	}
}

void tcpservice::setnonblock(int sockfd)
{
	if (0 > sockfd) return;
	int mode = 1;//1:非阻塞 0:阻塞
	if (0 > ioctl(sockfd, FIONBIO, &mode)) {
		log(3,"%s[%d] ioctl nonblock error[%d]:%s", __FUNCTION__, __LINE__, errno, strerror(errno));
	}
}

void tcpservice::addfd(int& epfd, int opfd)
{	
	struct sockaddr_in addr;
	struct sockaddr_in addr2;
	socklen_t socklen = sizeof(struct sockaddr);
	epoll_event event;
	
	mmutexep.lock();
	if (0 > opfd) goto error;
	if (0 > epfd) {
		epfd = epoll_create1(0);
	}

	event.data.fd = opfd;
	event.events  = EPOLLET | EPOLLIN;
	epoll_ctl(epfd, EPOLL_CTL_ADD, opfd, &event);
	mmapEpfd[epfd].push_back(opfd);
	
	bzero(&addr, sizeof(struct sockaddr_in));getsockname(opfd, (struct sockaddr *)(&addr), &socklen);
	bzero(&addr2, sizeof(struct sockaddr_in));getpeername(opfd, (struct sockaddr *)(&addr2), &socklen);
	log("%s[%d] addfd e%d-s%d a=%s:%d b=%s:%d",__FUNCTION__,__LINE__, epfd, opfd, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port),inet_ntoa(addr2.sin_addr), ntohs(addr2.sin_port));
	
error:
	mmutexep.unlock();
}

void tcpservice::delfd(int& epfd, int opfd)
{
	struct sockaddr_in addr;
	struct sockaddr_in addr2;
	socklen_t socklen = sizeof(struct sockaddr);
	epoll_event event;
	std::vector<int>::iterator result;
	
	mmutexep.lock();
	if (0 > opfd) goto error;
	if (0 > epfd) goto error;
	if (mmapEpfd.find(epfd) == mmapEpfd.end()) {
		log("%s[%d] delfd e%d not mapping", __FUNCTION__, __LINE__, epfd);
		goto error;
	}	

	epoll_ctl(epfd, EPOLL_CTL_DEL, opfd, &event);
	
	result = std::find(mmapEpfd[epfd].begin(), mmapEpfd[epfd].end(), opfd);
	if (result != mmapEpfd[epfd].end()) {
		mmapEpfd[epfd].erase(result);
		
		bzero(&addr, sizeof(struct sockaddr_in));getsockname(opfd, (struct sockaddr *)(&addr), &socklen);
		bzero(&addr2, sizeof(struct sockaddr_in));getpeername(opfd, (struct sockaddr *)(&addr2), &socklen);
		log("%s[%d] delfd e%d-s%d a=%s:%d b=%s:%d",__FUNCTION__,__LINE__, epfd, opfd, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port),inet_ntoa(addr2.sin_addr), ntohs(addr2.sin_port));
		close(opfd);
		opfd = -1;
	}
	if (mmapEpfd[epfd].size() == 0) {
		log("%s[%d] close e%d", __FUNCTION__, __LINE__, epfd);
		close(epfd);
		epfd = -1;
	}
	
error:
	mmutexep.unlock();
}

void tcpservice::clrfd(int& epfd)
{
	if (0 > epfd) return;
	std::map<int, std::vector<int> >::iterator itermap;
	std::vector<int>::iterator itervec;
	struct sockaddr_in addr;
	struct sockaddr_in addr2;
	socklen_t socklen = sizeof(struct sockaddr);
	epoll_event event;
	int opfd = -1;

	mmutexep.lock();
	itermap = mmapEpfd.find(epfd);
	if (itermap == mmapEpfd.end()) {
		goto error;
	}

	for (itervec = itermap->second.begin(); itervec != itermap->second.end(); ++itervec) {
		opfd = *itervec;
		if (0 > opfd) continue;
		bzero(&addr, sizeof(struct sockaddr_in));getsockname(opfd, (struct sockaddr *)(&addr), &socklen);
		bzero(&addr2, sizeof(struct sockaddr_in));getpeername(opfd, (struct sockaddr *)(&addr2), &socklen);
		log("%s[%d] delfd e%d-s%d a=%s:%d b=%s:%d",__FUNCTION__,__LINE__, epfd, opfd, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port),inet_ntoa(addr2.sin_addr), ntohs(addr2.sin_port));
		epoll_ctl(epfd, EPOLL_CTL_DEL, opfd, &event);
	}
	mmapEpfd[epfd].clear();
	close(epfd);
	epfd = -1;
	
error:
	mmutexep.unlock();	
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
