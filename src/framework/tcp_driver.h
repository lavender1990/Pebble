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


#ifndef _PEBBLE_TCP_DRIVER_H_
#define _PEBBLE_TCP_DRIVER_H_

#include "framework/message.h"
//#include "ev.h"

struct ev_loop;

namespace pebble {

class Connection;
class KVCache;
class Listener;


/// @brief RAW TCP/UDP网络驱动接口
/// @note 这里不考虑性能，仅供开发、测试使用，生产环境使用tbuspp
class TcpDriver : public MessageDriver {
public:
    TcpDriver();
	virtual ~TcpDriver();

    // 默认接收缓冲区为2M
    static const int32_t DEFAULT_COMMON_BUFF_LEN = 1024 * 1024 * 2;

    virtual int32_t Init();

    virtual int64_t Bind(const std::string& url);

    virtual int64_t Connect(const std::string& url);

    virtual int32_t Send(int64_t handle, const uint8_t* msg, uint32_t msg_len, int32_t flag);

    virtual int32_t SendV(int64_t handle, uint32_t msg_frag_num,
                          const uint8_t* msg_frag[], uint32_t msg_frag_len[], int32_t flag);

    virtual int32_t Close(int64_t handle);

    virtual int32_t Update();

	virtual const char* Prefix() const { return "tcp"; }

public:
	virtual int32_t ParseHead(const uint8_t* head, uint32_t head_len, uint32_t* data_len);

	virtual int32_t OnMessage(Connection* connection, const uint8_t* msg, uint32_t msg_len);

public:
	void Accept(int64_t handle, int fd);

	void CloseListener(int64_t handle);

	void CloseConnection(int64_t local_handle, int64_t trans_handle);

	KVCache* GetSendCache() { return m_send_cache; }

	KVCache* GetRecvCache() { return m_recv_cache; }

	char* GetCommonBuff() { return m_common_buff; }

protected:
	int32_t SendRaw(int64_t handle, uint32_t msg_frag_num, const uint8_t* msg_frag[], uint32_t msg_frag_len[]);

private:
	struct ev_loop* m_loop;
	KVCache* m_send_cache;
	KVCache* m_recv_cache;
	char* m_common_buff;

	cxx::unordered_map<int64_t, cxx::shared_ptr<Listener> > m_listeners;
	cxx::unordered_map<int64_t, cxx::shared_ptr<Connection> > m_connections;
};


} // namespace pebble

#endif // _PEBBLE_TCP_DRIVER_H_


