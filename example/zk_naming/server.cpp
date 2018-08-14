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

#include <iostream>
#include <stdio.h>
#include <stdlib.h>

#include "server/pebble.h"


#define ASSERT_EQ(expected, actual) \
    if ((expected) != (actual)) { \
        fprintf(stderr, "(%s:%d)(%s) %d != %d\n", \
            __FILE__, __LINE__, __FUNCTION__, (expected), (actual)); \
        exit(1); \
    }


int main(int argc, char** argv) {
	int ret = INSTALL_ZOOKEEPER_NAMING;
	ASSERT_EQ(0, ret);

	pebble::PebbleServer server;
	ret = server.LoadOptionsFromIni("./cfg/pebble.ini");
	ASSERT_EQ(0, ret);
	ret = server.Init();
	ASSERT_EQ(0, ret);

	pebble::ZookeeperNaming* zk_naming = dynamic_cast<pebble::ZookeeperNaming*>(server.GetNaming());
	ASSERT_EQ(true, zk_naming != NULL);

	ret = zk_naming->SetAppInfo("10000", "game_key");
	ASSERT_EQ(0, ret);

	if (argc > 1)
		ret = zk_naming->Register("/10000/abc", argv[1], 100);
	else
		ret = zk_naming->Register("/10000/abc", "tcp://127.0.0.1:8000", 100);
	ASSERT_EQ(0, ret);

	server.Serve();

    return 0;
}



