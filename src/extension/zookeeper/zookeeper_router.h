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


#ifndef _PEBBLE_ZOOKEEPER_ROUTER_H_
#define _PEBBLE_ZOOKEEPER_ROUTER_H_

#include <map>
#include "framework/message.h"
#include "framework/naming.h"
#include "framework/router.h"

namespace pebble {


class ZookeeperRouter : public Router
{
public:
    /// @brief 根据名字来生成路由
    /// @param name_path 名字的绝对路径，同@see Naming中的name_path
    explicit ZookeeperRouter(const std::string& name_path);

    virtual ~ZookeeperRouter();

    /// @brief 初始化
    /// @param naming 提供的Naming
    /// @return 0 成功，其它失败@see RouterErrorCode
    /// @note 具体的地址列表由Router从naming中获取，地址的变化也在Router中内部处理
    virtual int32_t Init(Naming* naming);

    /// @brief 根据实例ID获取handle，用于指定点对点路由场景
    /// @param id 服务器实例ID
    /// @return 非负数 - 成功，其它失败@see RouterErrorCode
    int64_t GetHandleByInstanceID(uint32_t id);

protected:
    void NameWatch(const std::string& name, const std::vector<std::string>& urls);

    std::map<uint32_t, int64_t>    m_id_2_handle;
};

class ZookeeperRouterFactory {
public:
    ZookeeperRouterFactory() {}
    virtual ~ZookeeperRouterFactory() {}
    virtual Router* GetRouter(const std::string& name_path) {
        return new Router(name_path);
    }
};


} // namespace pebble

#endif // _PEBBLE_ZOOKEEPER_ROUTER_H_
