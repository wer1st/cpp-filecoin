# Copyright Soramitsu Co., Ltd. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

addtest(datastore_integration_test
    datastore_integration_test.cpp
    )
target_link_libraries(datastore_integration_test
    p2p::p2p_random_generator
    ipfs_datastore_leveldb
    )

addtest(in_memory_ipfs_datastore_test
    in_memory_ipfs_datastore_test.cpp
    )
target_link_libraries(in_memory_ipfs_datastore_test
    p2p::p2p_random_generator
    ipfs_datastore_in_memory
    )

add_subdirectory(merkledag)
add_subdirectory(graphsync)
