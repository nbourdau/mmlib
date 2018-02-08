/*
   @mindmaze_header@
*/
#if HAVE_CONFIG_H
# include <config.h>
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include "mmsysio.h"
#include "mmerrno.h"
#include "mmpredefs.h"
#include "mmthread.h"
#include "utils-win32.h"

#include <winsock2.h>
#include <windows.h>
#include <synchapi.h>

// Work around missing definition is Mingw
#ifndef WSA_FLAG_NO_HANDLE_INHERIT
#define WSA_FLAG_NO_HANDLE_INHERIT      0x80
#endif

#define wrap_socket_into_fd(hnd, p_fd)  \
	wrap_handle_into_fd((HANDLE)(hnd), p_fd, FD_TYPE_SOCKET)

#define unwrap_socket_from_fd(p_hnd, fd) \
	unwrap_handle_from_fd((HANDLE*)(p_hnd), fd)

/**************************************************************************
 *                                                                        *
 *                      Winsock initialization                            *
 *                                                                        *
 **************************************************************************/
static
void winsock_deinit(void)
{
	WSACleanup();
}


static
void winsock_init(void)
{
	WSADATA wsa;

	if (WSAStartup(MAKEWORD(2,2), &wsa))
		mm_raise_from_w32err("WSAStartup() failed");

	atexit(winsock_deinit);
}


static
int check_wsa_init()
{
	static mmthr_once_t wsa_ini_once = MMTHR_ONCE_INIT;

	mmthr_once(&wsa_ini_once, winsock_init);

	return 0;
}


/**************************************************************************
 *                                                                        *
 *                      Overlapped operation helpers                      *
 *                                                                        *
 **************************************************************************/

/**
 * struct xfer_data - overlapped transfer data
 * @overlapped:	data needed by Winsock to handle the overlapped operation
 * @data_len:   reported transferred length when operation complete
 * @status:     indicator reporting when the operation is still in progress
 *              (XFER_PENDING) or completed by success (XFER_SUCCESS) or
 *              failure (XFER_ERROR)
 * @flags:      flags reported by the operation. Meaningful only when
 *              a reception complete
 */
struct xfer_data {
	WSAOVERLAPPED overlapped;
	size_t data_len;
	enum {XFER_PENDING, XFER_SUCCESS, XFER_ERROR} status;
	DWORD flags;
};
#define GET_XFER_DATA_FROM_LPO(lpo)    ((struct xfer_data*)(((char*)lpo)-offsetof(struct xfer_data, overlapped)))


/**
 * xfer_completion() - transfer completion callback
 * @error:      error reported by the complete operation (0 if success)
 * @xfer_sz:    size of the transferred data
 * @lpo:        WSAOVERLAPPED data used during the transfer
 * @flags:      flags set on return of received operation. Can be ignored
 *              for send operations
 */
static
void CALLBACK xfer_completion(DWORD error, DWORD xfer_sz,
                              LPWSAOVERLAPPED lpo, DWORD flags)
{
	struct xfer_data* xfer = GET_XFER_DATA_FROM_LPO(lpo);

	xfer->status = XFER_SUCCESS;
	xfer->flags = flags;
	xfer->data_len = xfer_sz;
	if (error) {
		xfer->status = XFER_ERROR;
		WSASetLastError(error);
	}
}


/**
 * wait_operation_xfer() - wait for a initiated transfer to complete
 * @xfer:       pointer to a transfer submitted previously
 *
 * This function waits until the transfer specified by @xfer complete, be it
 * by a success of the submitted operation or failure. The transfer can have
 * been submitted by submit_send_xfer() or submit_recv_xfer().
 *
 * Return: 0 the submitted operation completed successfully, -1 otherwise.
 */
static
int wait_operation_xfer(const struct xfer_data* xfer)
{
	while (xfer->status == XFER_PENDING) {
		SleepEx(INFINITE, TRUE);
	}

	return (xfer->status == XFER_SUCCESS) ? 0 : -1;
}


/**
 * submit_send_xfer() - initiate send based overlapped IO
 * @xfer:       pointer xfer_data that will keep track of the operation
 * @s:          WIN32 socket handle to use for send operation
 * @msg:        message structure to holding data to send
 * @flags:      type of message transmission
 *
 * Return: 0 in case of success, -1 otherwise (error state not set here!)
 */
static
int submit_send_xfer(struct xfer_data* xfer, SOCKET s,
                     const struct msghdr* msg, int flags)
{
	int ret;
	WSABUF* buffs;
	DWORD dwflags = flags;

	// Safe to do: struct iovec has been defined with the exact same
	// memory layout as WSABUF
	buffs = (WSABUF*)msg->msg_iov;

	xfer->status = XFER_PENDING;

	if (msg->msg_name == NULL) {
		ret = WSASend(s, buffs, msg->msg_iovlen, NULL, dwflags,
		              &xfer->overlapped, xfer_completion);
	} else {
		ret = WSASendTo(s, buffs, msg->msg_iovlen, NULL, dwflags,
		                msg->msg_name, msg->msg_namelen,
		                &xfer->overlapped, xfer_completion);
	}

	// Check overlapped IO submission has not failed
	if ((ret == SOCKET_ERROR) && (WSAGetLastError() != WSA_IO_PENDING))
		return -1;

	return 0;
}


/**
 * submit_recv_xfer() - initiate recv based overlapped IO
 * @xfer:       pointer xfer_data that will keep track of the operation
 * @s:          WIN32 socket handle to use for recv operation
 * @msg:        message structure to holding received data
 * @flags:      type of message transmission
 *
 * Return: 0 in case of success, -1 otherwise (error state not set here!)
 */
static
int submit_recv_xfer(struct xfer_data* xfer, SOCKET s,
                     struct msghdr* msg, int flags)
{
	int ret;
	WSABUF* buffs;
	DWORD dwflags = flags;

	// Safe to do: struct iovec has been defined with the exact same
	// memory layout as WSABUF
	buffs = (WSABUF*)msg->msg_iov;

	// Submit async recv. Oddly WSARecvFrom failed with UDP socket
	// when addr is NULL (while it works with TCP socket) proving MSDN
	// documention of WSARecvFrom() wrong
	xfer->status = XFER_PENDING;
	if (msg->msg_name == NULL) {
		ret = WSARecv(s, buffs, msg->msg_iovlen, NULL, &dwflags,
		              &xfer->overlapped, xfer_completion);
	} else {
		ret = WSARecvFrom(s, buffs, msg->msg_iovlen, NULL, &dwflags,
	                          msg->msg_name, &msg->msg_namelen,
		                  &xfer->overlapped, xfer_completion);
	}

	// Check overlapped IO submission has not failed
	if ((ret == SOCKET_ERROR) && (WSAGetLastError() != WSA_IO_PENDING))
		return -1;

	return 0;
}


/**************************************************************************
 *                                                                        *
 *                               Socket API                               *
 *                                                                        *
 **************************************************************************/

API_EXPORTED
int mm_socket(int domain, int type)
{
	SOCKET s;
	int sockfd;

	if (check_wsa_init())
		return -1;

	s = WSASocketW(domain, type, 0, NULL, 0,
	              WSA_FLAG_OVERLAPPED|WSA_FLAG_NO_HANDLE_INHERIT);
	if (s == INVALID_SOCKET)
		return mm_raise_from_w32err("WSAsocket failed");

	if (wrap_socket_into_fd(s, &sockfd)) {
		closesocket(s);
		return -1;
	}

	return sockfd;
}


API_EXPORTED
int mm_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
	SOCKET s;

	if ( check_wsa_init()
	  || unwrap_socket_from_fd(&s, sockfd) )
		return -1;

	if (bind(s, addr, addrlen) < 0)
		return mm_raise_from_w32err("bind() failed");

	return 0;
}


API_EXPORTED
int mm_listen(int sockfd, int backlog)
{
	SOCKET s;

	if ( check_wsa_init()
	  || unwrap_socket_from_fd(&s, sockfd) )
		return -1;

	if (listen(s, backlog) < 0)
		return mm_raise_from_w32err("listen() failed");

	return 0;
}


API_EXPORTED
int mm_accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen)
{
	SOCKET listening_sock, s;
	int conn_fd;

	if ( check_wsa_init()
	  || unwrap_socket_from_fd(&listening_sock, sockfd) )
		return -1;

	s = accept(listening_sock, addr, addrlen);
	if (s == INVALID_SOCKET)
		return mm_raise_from_w32err("accept() failed");

	if (wrap_socket_into_fd(s, &conn_fd)) {
		closesocket(s);
		return -1;
	}

	return conn_fd;
}


API_EXPORTED
int mm_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
	SOCKET s;

	if ( check_wsa_init()
	  || unwrap_socket_from_fd(&s, sockfd) )
		return -1;

	if (connect(s, addr, addrlen) < 0)
		return mm_raise_from_w32err("connect() failed");

	return 0;
}


API_EXPORTED
int mm_setsockopt(int sockfd, int level, int optname,
                  const void *optval, socklen_t optlen)
{
	SOCKET s;

	if ( check_wsa_init()
	  || unwrap_socket_from_fd(&s, sockfd) )
		return -1;

	if (setsockopt(s, level, optname, optval, optlen) < 0)
		return mm_raise_from_w32err("setsockopt() failed");

	return 0;
}


API_EXPORTED
int mm_getsockopt(int sockfd, int level, int optname,
                  void *optval, socklen_t* optlen)
{
	SOCKET s;

	if ( check_wsa_init()
	  || unwrap_socket_from_fd(&s, sockfd) )
		return -1;

	if (getsockopt(s, level, optname, optval, optlen) < 0)
		return mm_raise_from_w32err("getsockopt() failed");

	return 0;
}


API_EXPORTED
int mm_shutdown(int sockfd, int how)
{
	SOCKET s;

	if ( check_wsa_init()
	  || unwrap_socket_from_fd(&s, sockfd) )
		return -1;

	if (shutdown(s, how) < 0)
		mm_raise_from_w32err("shutdown() failed");

	return 0;
}


API_EXPORTED
ssize_t mm_send(int sockfd, const void *buffer, size_t length, int flags)
{
	SOCKET s;
	ssize_t ret_sz;

	if ( check_wsa_init()
	  || unwrap_socket_from_fd(&s, sockfd) )
		return -1;

	ret_sz = send(s, buffer, length, flags);
	if (ret_sz < 0)
		return mm_raise_from_w32err("send() failed");

	return ret_sz;
}


API_EXPORTED
ssize_t mm_recv(int sockfd, void *buffer, size_t length, int flags)
{
	SOCKET s;
	ssize_t ret_sz;

	if ( check_wsa_init()
	  || unwrap_socket_from_fd(&s, sockfd) )
		return -1;

	ret_sz = recv(s, buffer, length, flags);
	if (ret_sz < 0)
		return mm_raise_from_w32err("recv() failed");

	return ret_sz;
}


API_EXPORTED
ssize_t mm_sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
	SOCKET s;
	struct xfer_data xfer;
	const char* errmsg;

	if ( check_wsa_init()
	  || unwrap_socket_from_fd(&s, sockfd) )
		return -1;

	if ( submit_send_xfer(&xfer, s, msg, flags)
	  || wait_operation_xfer(&xfer) ) {
		errmsg = msg->msg_name ? "WsaSendTo() failed" : "WsaSend() failed";
		return mm_raise_from_w32err(errmsg);
	}

	return xfer.data_len;
}


API_EXPORTED
ssize_t mm_recvmsg(int sockfd, struct msghdr *msg, int flags)
{
	SOCKET s;
	struct xfer_data xfer;
	const char* errmsg;

	if ( check_wsa_init()
	  || unwrap_socket_from_fd(&s, sockfd) )
		return -1;

	if ( submit_recv_xfer(&xfer, s, msg, flags)
	  || wait_operation_xfer(&xfer) ) {
		errmsg = msg->msg_name ? "WsaRecvFrom() failed" : "WsaRecv() failed";
		return mm_raise_from_w32err(errmsg);
	}

	msg->msg_flags = xfer.flags;
	return xfer.data_len;
}


API_EXPORTED
int mm_send_multimsg(int sockfd, int vlen, struct mmsock_multimsg *msgvec,
                     int flags)
{
	int i;
	ssize_t ret_sz;

	// TODO implement mm_sendmmsg with multiple WSASendTo
	for (i = 0; i < vlen; i++) {
		ret_sz = mm_sendmsg(sockfd, &msgvec[i].msg, flags);
		if (ret_sz < 0)
			return (i == 0) ? -1 : i;

		msgvec[i].datalen = (unsigned int)ret_sz;
	}

	return vlen;
}


API_EXPORTED
int mm_recv_multimsg(int sockfd, int vlen, struct mmsock_multimsg *msgvec,
                     int flags, struct timespec *timeout)
{
	int i;
	ssize_t ret_sz;
	(void)timeout;

	// TODO implement mm_recvmmsg with multiple WSARecvFrom
	for (i = 0; i < vlen; i++) {
		ret_sz = mm_recvmsg(sockfd, &msgvec[i].msg, flags);
		if (ret_sz < 0)
			return (i == 0) ? -1 : i;

		msgvec[i].datalen = (unsigned int)ret_sz;
	}

	return vlen;
}


API_EXPORTED
int mm_getaddrinfo(const char *node, const char *service,
                   const struct addrinfo *hints,
		   struct addrinfo **res)
{
	if (check_wsa_init())
		return -1;

	if (getaddrinfo(node, service, hints, res) != 0)
		return mm_raise_from_w32err("getaddrinfo() failed");

	return 0;
}


API_EXPORTED
int mm_getnamedinfo(const struct sockaddr *addr, socklen_t addrlen,
                    char *host, socklen_t hostlen,
                    char *serv, socklen_t servlen, int flags)
{
	if (check_wsa_init())
		return -1;

	if (getnameinfo(addr, addrlen, host, hostlen, serv, servlen, flags) != 0)
		return mm_raise_from_w32err("getaddrinfo() failed");

	return 0;
}


API_EXPORTED
void mm_freeaddrinfo(struct addrinfo *res)
{
	freeaddrinfo(res);
}