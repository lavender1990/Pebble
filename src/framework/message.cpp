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

#include "common/log.h"
#include "framework/message.h"
#include "framework/tcp_driver.h"

namespace pebble {

#define HANDLE_SEQ_MASK 0x7000000000000000LL
#define HANDLE_SEQ_MASK_OFFSET 60
/*
	handle MASK:
		tcp	 : 0 << 60
		http : 1 << 60
		...
*/

static int endian_pos = 7;
typedef union {
	uint8_t	a8[8];
    int64_t i64;
} HandleHelper;


MessageCallbacks Message::m_cbs;
int Message::m_driver_num = 0;
cxx::shared_ptr<MessageDriver> Message::m_drivers[Message::MAX_DRIVER_NUM];
std::map<std::string, cxx::shared_ptr<MessageDriver> > Message::m_prefix_to_driver;


MessageDriver::MessageDriver() {
	m_handle_seq = 0;
	m_handle_mask = 0;
}

int64_t MessageDriver::GenHandle() {
	return m_handle_mask | m_handle_seq++;
}

int32_t Message::Init(const MessageCallbacks& cb) {
	int a = 1;
	(*(char *)&a == 1) ? endian_pos = 7 : endian_pos = 0;

	m_cbs = cb;

	cxx::shared_ptr<MessageDriver> tcp_driver(new TcpDriver());
    int ret = AddDriver(tcp_driver);
	if (ret != 0) {
		return ret;
	}
	// add other driver...

    return 0;
}

int64_t Message::Bind(const std::string &url) {
	size_t pos = url.find("://");
	if (std::string::npos == pos) {
		PLOG_ERROR("");
		return kMESSAGE_UNINSTALL_DRIVER;
	}
	std::map<std::string, cxx::shared_ptr<MessageDriver> >::iterator it =
		m_prefix_to_driver.find(url.substr(0, pos));
	if (it == m_prefix_to_driver.end()) {
		PLOG_ERROR("can't find %s's driver", url.c_str());
		return kMESSAGE_UNINSTALL_DRIVER;
	}

    return it->second->Bind(url.substr(pos + 3));
}

int64_t Message::Connect(const std::string &url) {
	size_t pos = url.find("://");
	if (std::string::npos == pos) {
		PLOG_ERROR("");
		return kMESSAGE_UNINSTALL_DRIVER;
	}
	std::map<std::string, cxx::shared_ptr<MessageDriver> >::iterator it =
		m_prefix_to_driver.find(url.substr(0, pos));
	if (it == m_prefix_to_driver.end()) {
		PLOG_ERROR("can't find %s's driver", url.c_str());
		return kMESSAGE_UNINSTALL_DRIVER;
	}

	return it->second->Connect(url.substr(pos + 3));
}

int32_t Message::Send(int64_t handle, const uint8_t* msg, uint32_t msg_len, int32_t flag) {
	cxx::shared_ptr<MessageDriver> driver = Message::GetDriver(handle);
	if (driver) {
		return driver->Send(handle, msg, msg_len, flag);
	}
    return kMESSAGE_UNINSTALL_DRIVER;
}

int32_t Message::SendV(int64_t handle, uint32_t msg_frag_num,
                       const uint8_t* msg_frag[], uint32_t msg_frag_len[], int flag) {
	cxx::shared_ptr<MessageDriver> driver = Message::GetDriver(handle);
	if (driver) {
		return driver->SendV(handle, msg_frag_num, msg_frag, msg_frag_len, flag);
	}
    return kMESSAGE_UNINSTALL_DRIVER;
}

int32_t Message::Close(int64_t handle) {
	cxx::shared_ptr<MessageDriver> driver = Message::GetDriver(handle);
	if (driver) {
		return driver->Close(handle);
	}
    return kMESSAGE_UNINSTALL_DRIVER;
}

int32_t Message::Update() {
	int num = 0;
    for (int i = 0; i < m_driver_num; i++) {
		num += m_drivers[i]->Update();
	}
    return num;
}

int32_t Message::AddDriver(cxx::shared_ptr<MessageDriver> driver) {
	if (!driver) {
		return kMESSAGE_INVAILD_PARAM;
	}

	if (m_driver_num >= MAX_DRIVER_NUM) {
		PLOG_ERROR("driver num %d exceed the upper limit", m_driver_num);
		return kMESSAGE_DRIVER_REGISTER_FAILED;
	}

	const char* prefix = driver->Prefix();
	std::map<std::string, cxx::shared_ptr<MessageDriver> >::iterator it = m_prefix_to_driver.find(prefix);
	if (it != m_prefix_to_driver.end()) {
		PLOG_ERROR("driver %s is already existed", prefix);
		return kMESSAGE_DRIVER_REGISTER_FAILED;
	}

	driver->SetCallBack(m_cbs);
	driver->SetHandleMask(((int64_t)m_driver_num) << HANDLE_SEQ_MASK_OFFSET);

	int ret = driver->Init();
	if (ret != 0) {
		PLOG_ERROR("driver %s init failed(%d)", prefix, ret);
		return kMESSAGE_DRIVER_REGISTER_FAILED;
	}
	
	m_drivers[m_driver_num++] = driver;
	m_prefix_to_driver[prefix] = driver;
    return 0;
}

cxx::shared_ptr<MessageDriver> Message::GetDriver(int64_t handle) {
	HandleHelper handlehelper;
	handlehelper.i64 = handle & HANDLE_SEQ_MASK;
	int idx = handlehelper.a8[endian_pos] >> 4;
	if (idx >= m_driver_num) {
		PLOG_ERROR("handle %ld invalid(driver idx %d invalid)", handle, idx);
		cxx::shared_ptr<MessageDriver> null_ptr;
		return null_ptr;
	}
	return m_drivers[idx];
}

} // namespace pebble
