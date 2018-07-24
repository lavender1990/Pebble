/*
 * Tencent is pleased to support the open source community by making Pebble available.
 * Copyright (C) 2016 THL A29 Limited, a Tencent company. All rights reserved.
 * Licensed under the MIT License (the "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 * http://opensource.org/licenses/MIT
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied. See the License for the specific language governing permissions and limitations under
 * the License.
 *
 */

#include <arpa/inet.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "common/kv_cache.h"
#include "common/log.h"
#include "common/string_utility.h"
#include "common/time_utility.h"
#include "ev.h"
#include "framework/tcp_driver.h"


namespace pebble {

#define OFFSETOF(TYPE, MEMBER) ((size_t)(&((TYPE*)0)->MEMBER))
#define CONTAINER(TYPE, MEMBER, pMember) (NULL == pMember ? NULL : ((TYPE*)((size_t)(pMember) - OFFSETOF(TYPE, MEMBER))))


#define TCP_HEAD_MAGIC 0xA5A5A5A5
#pragma pack(1)
/// @brief 对tcp传输数据增加消息头
struct TcpMsgHead {
    TcpMsgHead() : _magic(TCP_HEAD_MAGIC), _data_len(0) {}
    uint32_t _magic;
    uint32_t _data_len;
};
#pragma pack()


struct Listener {
protected:
	Listener() {}
public:
	Listener(TcpDriver* driver, struct ev_loop* loop, int64_t handle);
	~Listener();
	int Listen(const std::string& ip, uint16_t port);
	void Accept();

	bool 			_start_accept;
	int 			_fd;
	TcpDriver* 		_driver;
	struct ev_loop* _loop;
	ev_io 			_aw; 	// accept watcher
	int64_t			_handle;
	std::string		_ip;
	uint16_t		_port;
};

struct Connection {
protected:
	Connection() {}
public:
	Connection(TcpDriver* driver, struct ev_loop* loop, int64_t local, int64_t trans);
	~Connection();
	void Close();
	void RegisterWatcher(int fd);
	int Connect(const std::string& ip, uint16_t port);
	int ReConnect();
	void Recv();
	void SendCacheData();
	int SendV(uint32_t msg_frag_num, const uint8_t* msg_frag[], uint32_t msg_frag_len[]);
	void OnError();

	bool 			_start_read;
	bool 			_start_write;
	int 			_fd;
	TcpDriver* 		_driver;
	struct ev_loop* _loop;
	ev_io 			_rw; 	// read watcher
	ev_io 			_ww; 	// write watcher
	int64_t			_local_handle;
	int64_t			_trans_handle;
	std::string		_ip;
	uint16_t		_port;
};

int32_t UrlToIpPort(const std::string& url, std::string* ip, uint16_t* port) {
    if (NULL == ip || NULL == port) {
        return -1;
    }

    size_t pos = url.find_last_of(':');
    if (std::string::npos == pos || url.size() == pos) {
        return -1;
    }

    ip->assign(url.substr(0, pos));
    *port = static_cast<uint16_t>(atoi(url.substr(pos + 1).c_str()));
    return 0;
}

static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0) {
		PLOG_ERROR("fcntl getfl %d failed %d:%s", fd, errno, strerror(errno));
		return -1;
	}
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
		PLOG_ERROR("fcntl setfl %d failed %d:%s", fd, errno, strerror(errno));
		return -2;
	}
    return 0;
}

static void on_accept(EV_P_ ev_io *w, int revents) {
	Listener* listener = CONTAINER(Listener, _aw, w);
	listener->Accept();
}

static void on_read(EV_P_ ev_io *w, int revents) {
	Connection* connection = CONTAINER(Connection, _rw, w);
	connection->Recv();
}

static void on_write(EV_P_ ev_io *w, int revents) {
	Connection* connection = CONTAINER(Connection, _ww, w);
	connection->SendCacheData();
}

Listener::Listener(TcpDriver* driver, struct ev_loop* loop, int64_t handle)
	: _driver(driver), _loop(loop), _handle(handle) {
	_start_accept = false;
	_fd = -1;
	_port = 0;
}

Listener::~Listener() {
	if (_start_accept) 	{ ev_io_stop(_loop, &_aw); }
	if (_fd >= 0) 		{ close(_fd);	}
}

int Listener::Listen(const std::string& ip, uint16_t port) {
	// check
	if (_fd >= 0) {
		PLOG_ERROR("Already listen in fd %d", _fd);
        return -1;
	}

	// socket
    _fd = socket(AF_INET, SOCK_STREAM, 0);
    if (_fd < 0) {
        PLOG_ERROR("socket failed %d:%s", errno, strerror(errno));
        return -2;
    }

	// set nonblock
	if (set_nonblock(_fd) != 0) {
		return -3;
	}

	// option
	int flag = 1;
	if (0 != setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag))) {
		PLOG_ERROR("setsockopt %d failed %d:%s", _fd, errno, strerror(errno));
        return -4;
    }
	
	// TODO: TCP_NODELAY
	struct sockaddr_in socket_addr;
    bzero(&socket_addr, sizeof(socket_addr));
    socket_addr.sin_family = AF_INET;

    int rc = inet_aton(ip.c_str(), &(socket_addr.sin_addr));
    if (rc == 0) {
        PLOG_ERROR("ip %s is invalid", ip.c_str());
		return -5;
    }
    socket_addr.sin_port = htons(port);

    if (0 != bind(_fd, (struct sockaddr *)&socket_addr, sizeof(socket_addr))) {
		PLOG_ERROR("bind %d failed %d:%s", _fd, errno, strerror(errno));
        return -6;
    }

    if (0 != listen(_fd, 10240)) {
        PLOG_ERROR("listen %d failed %d:%s", _fd, errno, strerror(errno));
        return -7;
    }

	// register watcher
	ev_io_init(&_aw, on_accept, _fd, EV_READ);
	ev_io_start(_loop, &_aw);
	_start_accept = true;

	_ip = ip;
	_port = port;

	return 0;
}

void Listener::Accept() {
	struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int32_t fd = accept(_fd, (struct sockaddr*)(&addr), &addr_len);
    if (fd < 0) {
        if (errno == EBADF || errno == ENOTSOCK) {
            PLOG_ERROR_N_EVERY_SECOND(1, "unexpect errno[%d:%s] when accept close socket[%d]", errno, strerror(errno), _fd);
			// close Listener.
            _driver->CloseListener(_handle);
        }
        return;
    }

	if (set_nonblock(fd) != 0) {
		close(fd);
		return;
	}

	_driver->Accept(_handle, fd);
}

Connection::Connection(TcpDriver* driver, struct ev_loop* loop, int64_t local, int64_t trans)
	: _driver(driver), _loop(loop), _local_handle(local), _trans_handle(trans) {
	_start_read = false;
	_start_write = false;
	_fd = -1;
	_port = 0;
}

Connection::~Connection() {
	Close();
}

void Connection::Close() {
	if (_start_read)  { ev_io_stop(_loop, &_rw); _start_read = false;  }
	if (_start_write) { ev_io_stop(_loop, &_ww); _start_write = false; }
	if (_fd >= 0) 	  { close(_fd); _fd = -1; }
	_driver->GetSendCache()->Del(_trans_handle);
	_driver->GetRecvCache()->Del(_trans_handle);
}

void Connection::RegisterWatcher(int fd) {
	_start_read = true;
	_fd 		= fd;
	ev_io_init(&_rw, on_read, fd, EV_READ);
	ev_io_init(&_ww, on_write, fd, EV_WRITE);
	ev_io_start(_loop, &_rw);
}

int Connection::ReConnect() {
	Close();
	return Connect(_ip, _port);
}

void Connection::OnError() {
	if (_local_handle == _trans_handle) {
		ReConnect();
	} else {
		_driver->CloseConnection(_local_handle, _trans_handle);
	}
}

int Connection::Connect(const std::string& ip, uint16_t port) {
	// socket
	_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (_fd < 0) {
        PLOG_ERROR("socket failed %d:%s", errno, strerror(errno));
        return -1;
    }

	// set nonblock
	if (set_nonblock(_fd) != 0) {
		return -2;
	}

	// connect
    struct sockaddr_in socket_addr;
    bzero(&socket_addr, sizeof(socket_addr));
    socket_addr.sin_family = AF_INET;

    int rc = inet_aton(ip.c_str(), &(socket_addr.sin_addr));
    if (rc == 0) {
        PLOG_ERROR("ip %s is invalid", ip.c_str());
		return -3;
    }
    socket_addr.sin_port = htons(port);

    int ret = connect(_fd, reinterpret_cast<struct sockaddr*>(&socket_addr), sizeof(socket_addr));
    if (ret < 0 && errno != EINPROGRESS)
    {
        PLOG_ERROR("connect %d failed %d:%s", _fd, errno, strerror(errno));
        return -4;
    }

	RegisterWatcher(_fd);
	_ip = ip;
	_port = port;

	return 0;
}

void Connection::Recv() {
	char* buff = _driver->GetCommonBuff();
	int buff_len = TcpDriver::DEFAULT_COMMON_BUFF_LEN;
	KVCache* cache = _driver->GetRecvCache();

	// 1. get cache
	int cache_len = cache->Get(_trans_handle, buff, buff_len);
	if (cache_len >= buff_len || cache_len < 0) {
		PLOG_ERROR_N_EVERY_SECOND(1, "get %ld's cache failed %d", _trans_handle, cache_len);
		OnError();
		return;
	}

	// 2. recv
	int32_t recv_len = 0;
	do {
		recv_len = recv(_fd, buff + cache_len, buff_len, 0);
	} while (recv_len < 0 && errno == EINTR);

	if (recv_len == 0 || (recv_len < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
		PLOG_ERROR_N_EVERY_SECOND(1, "handle %ld fd %d recv error %d:%s", _trans_handle, _fd, errno, strerror(errno));
		OnError();
		return;
	}

	if (recv_len < 0) {
		cache->Put(_trans_handle, buff, cache_len);
		return;
	}

	// 3. proc
	int proc_len = _driver->OnMessage(this, (uint8_t*)buff, cache_len + recv_len);
	int rest_len = cache_len + recv_len - proc_len;
	if (rest_len > 0) {
		int ret = cache->Put(_trans_handle, buff + proc_len, rest_len);
		if (ret != 0) {
			PLOG_ERROR_N_EVERY_SECOND(1, "put %ld's cache failed %d, len = ", _trans_handle, ret, rest_len);
			OnError();
			return;
		}
	}
}

void Connection::SendCacheData() {
	char* buff = _driver->GetCommonBuff();
	int buff_len = TcpDriver::DEFAULT_COMMON_BUFF_LEN;
	KVCache* cache = _driver->GetSendCache();

	// 1. get cache
	int cache_len = cache->Get(_trans_handle, buff, buff_len);
	if (cache_len >= buff_len || cache_len < 0) {
		PLOG_ERROR_N_EVERY_SECOND(1, "get %ld's cache failed %d", _trans_handle, cache_len);
		OnError();
		return;
	}

	// 2. send
	int32_t send_ret = 0;
	int32_t send_cnt = 0;
	while (send_cnt < cache_len) {
		send_ret = send(_fd, buff + send_cnt, cache_len - send_cnt, 0);
		if ((send_ret < 0 && errno != EINTR) || send_ret == 0) {
			break;
		}
		send_cnt += send_ret;
	}

	// send complete
	if (send_cnt == cache_len) {
		ev_io_stop(_loop, &_ww);
		_start_write = false;
		return;
	}

	if (send_ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
		PLOG_ERROR_N_EVERY_SECOND(1, "send failed %d:%s, need close the socket[%d]", errno, strerror(errno), _fd);
		OnError();
		return;
	}

	// cache
	int rest_len = cache_len - send_ret;
	int ret = cache->Put(_trans_handle, buff + send_ret, rest_len);
	if (ret != 0) {
		PLOG_ERROR_N_EVERY_SECOND(1, "put %ld's cache failed %d, len = %d", _trans_handle, ret, rest_len);
		OnError();
		return;
	}
	ev_io_start(_loop, &_ww);
	_start_write = true;
}

int Connection::SendV(uint32_t msg_frag_num, const uint8_t* msg_frag[], uint32_t msg_frag_len[]) {
	if (_start_write) {
		KVCache* cache = _driver->GetSendCache();
		for (int i = 0; i < (int)msg_frag_num; i++) {
			int ret = cache->Put(_trans_handle, (char*)msg_frag[i], msg_frag_len[i]);
			if (ret != 0) {
				PLOG_ERROR_N_EVERY_SECOND(1, "put %ld's cache failed %d, len = ", _trans_handle, msg_frag_len[i]);
				OnError();
				return kMESSAGE_SYSTEM_ERROR;
			}
		}
		return 0;
	}
	if (_fd < 0 && ReConnect() < 0) {
		return -1;
	}
	
	int32_t send_ret = 0;
	uint32_t send_cnt = 0;
	uint32_t need_send_cnt = 0;
	struct msghdr msg;
	struct iovec msg_iov[Message::MAX_SENDV_DATA_NUM];
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = msg_iov;
	msg.msg_iovlen = msg_frag_num;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;
	for (uint32_t i = 0 ; i < msg_frag_num && i < Message::MAX_SENDV_DATA_NUM; ++i) {
		msg_iov[i].iov_base = (char*)msg_frag[i];
		msg_iov[i].iov_len	= msg_frag_len[i];
		need_send_cnt += msg_frag_len[i];
	}

	while (send_cnt < need_send_cnt) {
		while (send_ret > 0) {
			if (msg.msg_iov->iov_len <= static_cast<uint32_t>(send_ret)) {
				send_ret -= msg.msg_iov->iov_len;
				msg.msg_iov++;
				msg.msg_iovlen--;
			} else {
				msg.msg_iov->iov_base = ((uint8_t*)(msg.msg_iov->iov_base)) + send_ret;
				msg.msg_iov->iov_len -= send_ret;
				break;
			}
		}
		send_ret = sendmsg(_fd, &msg, 0);
		if ((send_ret < 0 && errno != EINTR) || send_ret == 0) {
			break;
		}
		send_cnt += send_ret;
	}

	if (send_cnt == need_send_cnt) {
		return 0;
	}

	if (send_ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
		PLOG_ERROR_N_EVERY_SECOND(1, "send failed %d:%s, close the socket[%d]", errno, strerror(errno), _fd);
		OnError();
		return -1;
	}

	ev_io_start(_loop, &_ww);
	_start_write = true;

	return 0;
}

TcpDriver::TcpDriver() {
	m_loop 			= NULL;
	m_recv_cache	= NULL;
	m_common_buff	= NULL;
}

TcpDriver::~TcpDriver() {
	m_connections.clear();
	m_listeners.clear();

    ev_loop_destroy(m_loop);

	delete m_send_cache;
	m_send_cache = NULL;
	delete m_recv_cache;
	m_recv_cache = NULL;

	delete [] m_common_buff;
	m_common_buff = NULL;
}

int32_t TcpDriver::Init() {
	m_send_cache = new KVCache();
	int ret = m_send_cache->Init(100000, 20000, 2048);
	if (ret != 0) {
		PLOG_ERROR("cache init failed(%d)", ret);
		return kMESSAGE_SYSTEM_ERROR;
	}

	m_recv_cache = new KVCache();
	ret = m_recv_cache->Init(100000, 20000, 2048);
	if (ret != 0) {
		PLOG_ERROR("cache init failed(%d)", ret);
		return kMESSAGE_SYSTEM_ERROR;
	}

	m_common_buff = new char[DEFAULT_COMMON_BUFF_LEN];

	m_loop = ev_default_loop(0); // TODO: NEW, confict with business

	signal(SIGPIPE, SIG_IGN);

	return 0;
}

int64_t TcpDriver::Bind(const std::string& url) {
	// get ip port
	std::string ip;
    uint16_t port = 0;
    if (UrlToIpPort(url, &ip, &port) != 0) {
        return kMESSAGE_INVAILD_PARAM;
    }

	int64_t handle = GenHandle();
	if (handle < 0) {
		PLOG_ERROR("gen handle %ld invalid", handle);
		return kMESSAGE_SYSTEM_ERROR;
	}

	cxx::shared_ptr<Listener> listener(new Listener(this, m_loop, handle));
	if (listener->Listen(ip, port) != 0) {
		return kMESSAGE_BIND_ADDR_FAILED;
	}

	m_listeners[handle] = listener;

	return handle;
}

int64_t TcpDriver::Connect(const std::string& url) {
	// get ip port
    std::string ip;
    uint16_t port = 0;
    if (UrlToIpPort(url, &ip, &port) != 0) {
        return kMESSAGE_INVAILD_PARAM;
    }

	int64_t handle = GenHandle();
	if (handle < 0) {
		PLOG_ERROR("gen handle %ld invalid", handle);
		return kMESSAGE_SYSTEM_ERROR;
	}

	cxx::shared_ptr<Connection> connection(new Connection(this, m_loop, handle, handle));
	if (connection->Connect(ip, port) != 0) {
		return kMESSAGE_CONNECT_ADDR_FAILED;
	}

	m_connections[handle] = connection;

	return handle;
}

int32_t TcpDriver::Send(int64_t handle, const uint8_t* msg, uint32_t msg_len, int32_t flag) {
    const uint8_t* frags[1] = { msg     };
    uint32_t fragslen[1]    = { msg_len };

	return SendV(handle, 1, frags, fragslen, flag);
}

int32_t TcpDriver::SendV(int64_t handle, uint32_t msg_frag_num,
                          const uint8_t* msg_frag[], uint32_t msg_frag_len[], int32_t flag) {
    if (msg_frag_num + 1 > Message::MAX_SENDV_DATA_NUM) {
        PLOG_ERROR_N_EVERY_SECOND(1, "msg_frag_num %d > MAX_FRAG %d", msg_frag_num, Message::MAX_SENDV_DATA_NUM);
        return kMESSAGE_SYSTEM_ERROR;
    }

    const uint8_t* tmp_frags[Message::MAX_SENDV_DATA_NUM] = {0};
    uint32_t tmp_frag_len[Message::MAX_SENDV_DATA_NUM] = {0};

    int32_t msg_len = 0;
    for (uint32_t i = 0; i < msg_frag_num; i++) {
        msg_len += msg_frag_len[i];

        tmp_frags[ i + 1 ]    = msg_frag[i];
        tmp_frag_len[ i + 1 ] = msg_frag_len[i];
    }

    TcpMsgHead head;
    head._magic    = htonl(head._magic);
    head._data_len = htonl(msg_len);

    tmp_frags[0]    = (uint8_t*)(&head);
    tmp_frag_len[0] = sizeof(TcpMsgHead);

	return SendRaw(handle, msg_frag_num + 1, tmp_frags, tmp_frag_len);
}

int32_t TcpDriver::SendRaw(int64_t handle, uint32_t msg_frag_num, const uint8_t* msg_frag[], uint32_t msg_frag_len[]) {
	cxx::unordered_map<int64_t, cxx::shared_ptr<Connection> >::iterator it = m_connections.find(handle);
	if (m_connections.end() == it) {
		return kMESSAGE_INVAILD_HANDLE;
	}

	return it->second->SendV(msg_frag_num, msg_frag, msg_frag_len);
}

int32_t TcpDriver::Close(int64_t handle) {
	m_listeners.erase(handle);
	m_connections.erase(handle);
	return 0;
}

int32_t TcpDriver::Update() {
	int cnt = ev_run(m_loop, EVRUN_NOWAIT);
	return cnt;
}

int32_t TcpDriver::ParseHead(const uint8_t* head, uint32_t head_len, uint32_t* data_len) {
    if (head == NULL || data_len == NULL || head_len < sizeof(TcpMsgHead)) {
        return -1;
    }
    TcpMsgHead* msg_head = (TcpMsgHead*)head;
    if (ntohl(msg_head->_magic) != TCP_HEAD_MAGIC) {
		PLOG_ERROR_N_EVERY_SECOND(1, "msg magic %x invalid", msg_head->_magic);
        return -2;
    }
    // 暂无版本检查
    *data_len = ntohl(msg_head->_data_len);
    return sizeof(TcpMsgHead);
}

void TcpDriver::Accept(int64_t handle, int fd) {
	int64_t peer = GenHandle();
	if (peer < 0) {
		PLOG_ERROR("gen handle %ld invalid", peer);
		return;
	}
	
	cxx::shared_ptr<Connection> connection(new Connection(this, m_loop, handle, peer));
	connection->RegisterWatcher(fd);

	m_connections[peer] = connection;

	if (m_cbs._on_peer_connected) {
		m_cbs._on_peer_connected(handle, peer);
	}
}

void TcpDriver::CloseListener(int64_t handle) {
	m_listeners.erase(handle);
	if (m_cbs._on_closed) {
		m_cbs._on_closed(handle);
	}
}

void TcpDriver::CloseConnection(int64_t local_handle, int64_t trans_handle) {
	m_connections.erase(trans_handle);
	if (local_handle == trans_handle) {
		if (m_cbs._on_closed) {
			m_cbs._on_closed(local_handle);
		}
	} else {
		if (m_cbs._on_peer_closed) {
			m_cbs._on_peer_closed(local_handle, trans_handle);
		}
	}
}

int32_t TcpDriver::OnMessage(Connection* connection, const uint8_t* msg, uint32_t msg_len) {
	const uint8_t* buff = msg;
	uint32_t buff_len = msg_len;
	int32_t  proc_len = 0;
	do {
		uint32_t data_len = 0;
		int head_len = ParseHead(buff, buff_len, &data_len);
		if (head_len < 0) {
			break;
		}
		if (data_len + head_len > buff_len) {
			break;
		}
		buff += head_len;
		buff_len -= head_len;
		if (m_cbs._on_message) {
			MsgExternInfo msg_info;
			msg_info._self_handle 	 = connection->_local_handle;
			msg_info._remote_handle  = connection->_trans_handle;
			msg_info._msg_arrived_ms = TimeUtility::GetCurrentMS();
			m_cbs._on_message(buff, data_len, &msg_info);
			buff += data_len;
			buff_len -= data_len;
		}
		proc_len += head_len + data_len;
	} while (true); // TODO: avoid other connection hunger

	return proc_len;
}


} // namespace pebble
