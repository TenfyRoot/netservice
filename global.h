#ifndef GLOBAL_H
#define GLOBAL_H

typedef unsigned int        DWORD;
typedef int                 BOOL;
typedef unsigned int        UINT;

// 服务器地址和端口号定义
#define SRVTCPMAINPORT		4000				// 服务器主连接的端口号
#define SRVTCPHOLEPORT		8000				// 服务器响应客户端打洞申请的端口号

// 数据包类型
typedef enum _packet_type
{
	PACKET_TYPE_INVALID,
	PACKET_TYPE_NEW_USER_LOGIN,			// 服务器收到新的客户端登录，将登录信息发送给其他客户端
	PACKET_TYPE_WELCOME,				// 客户端登录时服务器发送该欢迎信息给客户端，以告知客户端登录成功
	PACKET_TYPE_REQUEST_CONN_CLIENT,	// 某客户端向服务器申请，要求与另一个客户端建立直接的TCP连接，即需要进行TCP打洞
	PACKET_TYPE_REQUEST_MAKE_HOLE,		// 服务器请求某客户端向另一客户端进行TCP打洞，即向另一客户端指定的外部IP和端口号进行connect尝试
	PACKET_TYPE_REQUEST_DISCONNECT,		// 请求服务器断开连接
	PACKET_TYPE_TCP_DIRECT_CONNECT,		// 服务器要求主动端（客户端A）直接连接被动端（客户端B）的外部IP和端口号
	PACKET_TYPE_HOLE_LISTEN_READY,		// 被动端（客户端B）打洞和侦听均已准备就绪

} PACKET_TYPE;

//
// 新用户登录数据
//
typedef struct _new_user_login
{
	_new_user_login ()
		: ePacketType ( PACKET_TYPE_NEW_USER_LOGIN )
		, nClientPort ( 0 )
		, dwID ( 0 )
	{
		memset ( szClientIP, 0, sizeof(szClientIP) );
	}
	PACKET_TYPE ePacketType;			// 包类型
	char szClientIP[32];				// 新登录的客户端（客户端B）外部IP地址
	UINT nClientPort;					// 新登录的客户端（客户端B）外部端口号
	DWORD dwID;							// 新登录的客户端（客户端B）的ID号（从1开始编号的一个唯一序号）
} tagNewUserLoginPkt;

//
// 欢迎信息
//
typedef struct _welcome
{
	_welcome ()
		: ePacketType ( PACKET_TYPE_WELCOME )
		, nClientPort ( 0 )
		, dwID ( 0 )
	{
		memset ( szClientIP, 0, sizeof(szClientIP) );
		memset ( szWelcomeInfo, 0, sizeof(szWelcomeInfo) );
	}
	PACKET_TYPE ePacketType;			// 包类型
	char szClientIP[32];				// 接收欢迎信息的客户端外部IP地址
	UINT nClientPort;					// 接收欢迎信息的客户端外部端口号
	DWORD dwID;							// 接收欢迎信息的客户端的ID号（从1开始编号的一个唯一序号）
	char szWelcomeInfo[64];				// 欢迎信息文本
} tagWelcomePkt;

//
// 客户端A请求服务器协助连接客户端B
//
typedef struct _req_conn_client
{
	_req_conn_client ()
		: ePacketType ( PACKET_TYPE_REQUEST_CONN_CLIENT )
		, dwInviterID ( 0 )
		, dwInvitedID ( 0 )
	{
	}
	PACKET_TYPE ePacketType;			// 包类型
	DWORD dwInviterID;					// 发出邀请方（主动方即客户端A）ID号
	DWORD dwInvitedID;					// 被邀请方（被动方即客户端B）ID号
} tagReqConnClientPkt;

//
// 服务器请求客户端B打洞
//
typedef struct _srv_req_make_hole
{
	_srv_req_make_hole ()
		: ePacketType ( PACKET_TYPE_REQUEST_MAKE_HOLE )
		, dwInviterID ( 0 )
		, dwInviterHoleID ( 0 )
		, dwInvitedID ( 0 )
		, nClientHolePort ( 0 )
		, nBindPort ( 0 )
	{
		memset ( szClientHoleIP, 0, sizeof(szClientHoleIP) );
	}
	PACKET_TYPE ePacketType;			// 包类型
	DWORD dwInviterID;					// 发出邀请方（主动方即客户端A）ID号
	DWORD dwInviterHoleID;				// 发出邀请方（主动方即客户端A）打洞ID号
	DWORD dwInvitedID;					// 被邀请方（被动方即客户端B）ID号
	char szClientHoleIP[32];			// 可以向该IP（请求方的外部IP）地址打洞，即发生一次connect尝试
	UINT nClientHolePort;				// 可以向该端口号（请求方的外部端口号）打洞，即发生一次connect尝试
	UINT nBindPort;
} tagSrvReqMakeHolePkt;

//
// 请求服务器断开连接
//
typedef struct _req_srv_disconnect
{
	_req_srv_disconnect ()
		: ePacketType ( PACKET_TYPE_REQUEST_DISCONNECT )
		, dwInviterID ( 0 )
		, dwInviterHoleID ( 0 )
		, dwInvitedID ( 0 )
	{
	}
	PACKET_TYPE ePacketType;			// 包类型
	DWORD dwInviterID;					// 发出邀请方（主动方即客户端A）ID号
	DWORD dwInviterHoleID;				// 发出邀请方（主动方即客户端A）打洞ID号
	DWORD dwInvitedID;					// 被邀请方（被动方即客户端B）ID号
} tagReqSrvDisconnectPkt;

//
// 服务器要求主动端（客户端A）直接连接被动端（客户端B）的外部IP和端口号
//
typedef struct _srv_req_tcp_direct_connect
{
	_srv_req_tcp_direct_connect ()
		: ePacketType ( PACKET_TYPE_TCP_DIRECT_CONNECT )
		, dwInvitedID ( 0 )
		, nInvitedPort ( 0 )
	{
		memset ( szInvitedIP, 0, sizeof(szInvitedIP) );
	}
	PACKET_TYPE ePacketType;			// 包类型
	DWORD dwInvitedID;					// 被邀请方（被动方即客户端B）ID号
	char szInvitedIP[32];				// 可以与该IP（被邀请方客户端B的外部IP）地址直接建立TCP连接
	UINT nInvitedPort;					// 可以与该端口号（被邀请方客户端B的外部IP）地址直接建立TCP连接
} tagSrvReqDirectConnectPkt;

//
// 被动端（客户端B）打洞和侦听均已准备就绪
//
typedef struct _hole_listen_ready
{
	_hole_listen_ready ()
		: ePacketType ( PACKET_TYPE_HOLE_LISTEN_READY )
		, dwInviterID ( 0 )
		, dwInviterHoleID ( 0 )
		, dwInvitedID ( 0 )
	{
	}
	PACKET_TYPE ePacketType;			// 包类型
	DWORD dwInviterID;					// 发出邀请方（主动方即客户端A）ID号
	DWORD dwInviterHoleID;				// 发出邀请方（主动方即客户端A）打洞ID号
	DWORD dwInvitedID;					// 被邀请方（被动方即客户端B）ID号
} tagHoleListenReadyPkt;

#endif
