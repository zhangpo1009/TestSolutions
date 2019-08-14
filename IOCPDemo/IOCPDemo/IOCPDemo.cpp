// IOCPDemo.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <thread>
#include <winsock2.h>

#include <windows.h>
#include <vector>
#include <mswsock.h>    //微软扩展的类库

#pragma comment(lib,"Ws2_32.lib")

#define MAX_BUFFER_LEN 1024*8

enum OPERATION_TYPE
{
	Recv = 0,
	Send,
	Accept,
};

struct _PER_IO_CONTEXT
{
	OVERLAPPED   m_Overlapped;
	SOCKET       m_sockClient;
	SOCKET       m_sockAccept;
	WSABUF       m_wsaBuf;
	char         m_szBuffer[MAX_BUFFER_LEN];
	DWORD		 m_len;
	OPERATION_TYPE  m_OpType;

	_PER_IO_CONTEXT()
	{
		m_sockAccept = -1;
		memset(&m_Overlapped, 0, sizeof(m_Overlapped));
		m_wsaBuf.len = MAX_BUFFER_LEN;
		memset(m_szBuffer, 0, sizeof(m_szBuffer));
		m_wsaBuf.buf = (char*)m_szBuffer;
	}

	void reset()
	{
		m_sockAccept = -1;
		memset(&m_Overlapped, 0, sizeof(m_Overlapped));
		m_wsaBuf.len = MAX_BUFFER_LEN;
		memset(m_szBuffer, 0, sizeof(m_szBuffer));
		m_wsaBuf.buf = (char*)m_szBuffer;
	}
};


struct _PER_SOCKET_CONTEXT
{
	SOCKET socket;
	SOCKADDR_IN addr;
	_PER_IO_CONTEXT recvIo;
};

std::vector<_PER_SOCKET_CONTEXT*> g_socketVec;

LPFN_ACCEPTEX lpfnAcceptEx = NULL;					 //AcceptEx函数指针
LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockaddrs;  //加载GetAcceptExSockaddrs函数指针

SOCKET hListen = 0;


int NewAccept()
{
	//创建socket
	SOCKET _socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	printf_s("new ####%d\n", _socket);
	_PER_IO_CONTEXT *ov = new _PER_IO_CONTEXT();
	memset(ov, 0, sizeof(ov));
	ov->m_OpType = OPERATION_TYPE::Accept;
	ov->m_sockClient = _socket;
	ov->m_sockAccept = hListen;

	//存放网络地址的长度
	int addrLen = sizeof(sockaddr_in) + 16;
	DWORD bytes = 0;
	int bRetVal = lpfnAcceptEx(hListen, _socket, ov->m_szBuffer,
		0, addrLen, addrLen,
		&bytes, &ov->m_Overlapped);
	if (bRetVal == FALSE)
	{
		int error = WSAGetLastError();
		if (error != WSA_IO_PENDING)
		{
			closesocket(_socket);
			return 0;
		}
	}

	return 1;
}

DWORD WINAPI AcceptExThreadPool(PVOID pContext)
{
	NewAccept();
	return 0;
}


int main()
{
	SYSTEM_INFO info;
	GetSystemInfo(&info);

	DWORD pNum = info.dwNumberOfProcessors * 2 + 2;
	HANDLE hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, pNum);

	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);

	hListen = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (hListen == INVALID_SOCKET) return 0;

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(10000);
	addr.sin_addr.S_un.S_addr = INADDR_ANY;
	int ret = bind(hListen, (struct sockaddr*)&addr, sizeof(addr));
	if (ret == SOCKET_ERROR) return 0;

	ret = listen(hListen, SOMAXCONN);
	if (ret == SOCKET_ERROR) return 0;

	DWORD dwbytes = 0;
	//Accept function GUID
	GUID guidAcceptEx = WSAID_ACCEPTEX;

	if (0 != WSAIoctl(hListen, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guidAcceptEx, sizeof(guidAcceptEx),
		&lpfnAcceptEx, sizeof(lpfnAcceptEx),
		&dwbytes, NULL, NULL))
	{
	}

	// 获取GetAcceptExSockAddrs函数指针，也是同理
	GUID guidGetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
	if (0 != WSAIoctl(hListen, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guidGetAcceptExSockaddrs,
		sizeof(guidGetAcceptExSockaddrs),
		&lpfnGetAcceptExSockaddrs,
		sizeof(lpfnGetAcceptExSockaddrs),
		&dwbytes, NULL, NULL))
	{
	}

	_PER_SOCKET_CONTEXT* per_socket = new _PER_SOCKET_CONTEXT;
	per_socket->socket = hListen;

	CreateIoCompletionPort((HANDLE)hListen, hIOCP, (ULONG_PTR)per_socket, 0);

	for (int i = 0; i < 3; i++)
		NewAccept();

	/*std::thread tAccept([&]() {
	while (1)
	{


	sockaddr_in adr;
	int len = sizeof(sockaddr_in);
	SOCKET hItem = WSAAccept(hListen, (struct sockaddr*)&adr, &len, NULL, NULL);
	if (hItem == INVALID_SOCKET) continue;

	_PER_SOCKET_CONTEXT* per_socket = new _PER_SOCKET_CONTEXT;
	per_socket->socket = hItem;
	per_socket->addr = adr;
	g_socketVec.push_back(per_socket);

	DWORD byte = 0;
	DWORD flag = 0;
	_PER_IO_CONTEXT& per_io = per_socket->recvIo;
	per_io.m_OpType = OPERATION_TYPE::Recv;

	CreateIoCompletionPort((HANDLE)hItem, hIOCP, (ULONG_PTR)per_socket, 0);

	bool iRetCode =  WSARecv(hItem, &per_io.m_wsaBuf, 1, &byte, &flag, &per_io.m_Overlapped, NULL);
	if ((iRetCode == SOCKET_ERROR) && (WSAGetLastError() != WSA_IO_PENDING))
	{
	closesocket(hItem);
	}
	}
	});*/




	auto f = [&]() {

		while (1)
		{
			DWORD NumberOfBytesTransferred = 0;
			OVERLAPPED* Overlapped = NULL;
			_PER_SOCKET_CONTEXT* perSocket = NULL;
			bool bsucc = GetQueuedCompletionStatus(hIOCP, &NumberOfBytesTransferred, (PULONG_PTR)&perSocket, &Overlapped, INFINITE);
			if (!bsucc && GetLastError() != ERROR_NETNAME_DELETED) continue;
			if (!perSocket || !Overlapped) continue;
			_PER_IO_CONTEXT* pSocketLapped = CONTAINING_RECORD(Overlapped, _PER_IO_CONTEXT, m_Overlapped);

			if (pSocketLapped->m_OpType == OPERATION_TYPE::Recv)
			{
				printf_s("[%s](%d) from client: %d\n", pSocketLapped->m_szBuffer, NumberOfBytesTransferred, perSocket->socket);

				DWORD byte = 0;
				DWORD flag = 0;
				_PER_IO_CONTEXT& per_io = perSocket->recvIo;
				per_io.reset();

				per_io.m_OpType = OPERATION_TYPE::Recv;
				bool bRetCode = WSARecv(perSocket->socket, &per_io.m_wsaBuf, 1, &byte, &flag, &per_io.m_Overlapped, NULL);
				if ((bRetCode == SOCKET_ERROR) && (WSAGetLastError() != WSA_IO_PENDING))
				{
					closesocket(perSocket->socket);
				}
			}
			else if (pSocketLapped->m_OpType == OPERATION_TYPE::Accept)
			{
				QueueUserWorkItem(AcceptExThreadPool, NULL, 0);

				SOCKADDR_IN* ClientAddr = NULL;
				SOCKADDR_IN* LocalAddr = NULL;
				int remoteLen = sizeof(SOCKADDR_IN), localLen = sizeof(SOCKADDR_IN);
				lpfnGetAcceptExSockaddrs(pSocketLapped->m_szBuffer, 0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, (LPSOCKADDR*)&LocalAddr, &localLen, (LPSOCKADDR*)&ClientAddr, &remoteLen);
				printf_s("client %d ,ip = %d.%d.%d.%d  connect\n", pSocketLapped->m_sockClient, ClientAddr->sin_addr.S_un.S_un_b.s_b1, ClientAddr->sin_addr.S_un.S_un_b.s_b2, ClientAddr->sin_addr.S_un.S_un_b.s_b3, ClientAddr->sin_addr.S_un.S_un_b.s_b4);

				_PER_SOCKET_CONTEXT* per_socket = new _PER_SOCKET_CONTEXT;
				per_socket->socket = pSocketLapped->m_sockClient;
				per_socket->addr = *ClientAddr;
				g_socketVec.push_back(per_socket);

				DWORD byte = 0;
				DWORD flag = 0;
				_PER_IO_CONTEXT& per_io = per_socket->recvIo;
				per_io.m_OpType = OPERATION_TYPE::Recv;

				CreateIoCompletionPort((HANDLE)pSocketLapped->m_sockClient, hIOCP, (ULONG_PTR)per_socket, 0);

				char buf[100] = { 0 };
				send(pSocketLapped->m_sockClient, buf, 100, 0);

				bool iRetCode = WSARecv(pSocketLapped->m_sockClient, &per_io.m_wsaBuf, 1, &byte, &flag, &per_io.m_Overlapped, NULL);
				if ((iRetCode == SOCKET_ERROR) && (WSAGetLastError() != WSA_IO_PENDING))
				{
					closesocket(pSocketLapped->m_sockClient);
				}
			}
			else
			{

			}
		}
	};


	for (int i = 0; i < pNum; i++)
	{
		std::thread t(f);
		t.detach();
	}


	std::this_thread::sleep_for(std::chrono::seconds(1));


	//tAccept.join();

	getchar();
	return 0;
}

