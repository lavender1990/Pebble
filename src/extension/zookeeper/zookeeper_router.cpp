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
#include <stdlib.h>
#include "common/string_utility.h"
#include "extension/zookeeper/zookeeper_router.h"

namespace pebble {

ZookeeperRouter::ZookeeperRouter(const std::string& name_path)
    :   Router(name_path)
{
}

ZookeeperRouter::~ZookeeperRouter()
{
}

int32_t ZookeeperRouter::Init(Naming* naming)
{
    if (NULL == naming) {
        return kROUTER_INVAILD_PARAM;
    }
    if (NULL != m_naming) {
        m_naming->UnWatchName(m_route_name);
    }
    m_naming = naming;
    CbNodeChanged cob = cxx::bind(&ZookeeperRouter::NameWatch, this, cxx::placeholders::_1, cxx::placeholders::_2);
    int32_t ret = m_naming->WatchName(m_route_name, cob);
    if (0 != ret) {
        return kROUTER_INVAILD_PARAM;
    }
    return SetRoutePolicy(m_route_type, m_route_policy);
}

int64_t ZookeeperRouter::GetHandleByInstanceID(uint32_t id)
{
    int64_t handle = -1;
    std::map<uint32_t, int64_t>::iterator it = m_id_2_handle.find(id);
    if (it != m_id_2_handle.end()) {
        handle = it->second;
    }
    return handle;
}

void ZookeeperRouter::NameWatch(const std::string& name, const std::vector<std::string>& urls)
{
    // TODO: 后续优化，目前实现有点粗暴
    for (uint32_t idx = 0 ; idx < m_route_handles.size() ; ++idx) {
        Message::Close(m_route_handles[idx]);
    }
    m_route_handles.clear();
    m_id_2_handle.clear();

    //P2P模式的临时方案：服务器注册地址时带上自己的id，如 tcp://127.0.0.1:8000@1001，其中1001为实例id
    for (uint32_t idx = 0 ; idx < urls.size() ; ++idx) {
        std::vector<std::string> vec;
        StringUtility::Split(urls[idx], "@", &vec);
        int64_t handle = Message::Connect(urls[idx]);
        if (handle < 0) {
            continue;
        }
        m_route_handles.push_back(handle);
        
        if (vec.size() > 1) {
            int id = atoi(vec[1].c_str());
            //TODO:重复覆盖是否合理？
            m_id_2_handle[id] = handle;
        }
    }

    if (m_on_address_changed) {
        m_on_address_changed(m_route_handles);
    }
}

} // namespace pebble
