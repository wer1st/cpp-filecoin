/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FILECOIN_CORE_STORAGE_REPOSITORY_HPP
#define FILECOIN_CORE_STORAGE_REPOSITORY_HPP

#include "boost/filesystem.hpp"
#include "common/outcome.hpp"
#include "sector_storage/stores/storage.hpp"
#include "storage/config/config.hpp"
#include "storage/ipfs/datastore.hpp"
#include "storage/keystore/keystore.hpp"

namespace fc::storage::repository {

  using fc::primitives::FsStat;
  using fc::sector_storage::stores::LocalStorage;
  using fc::sector_storage::stores::StorageConfig;
  using fc::storage::config::Config;
  using fc::storage::ipfs::IpfsDatastore;
  using fc::storage::keystore::KeyStore;

  /**
   * @brief Class represents all persistent data on node
   */
  class Repository : public LocalStorage {
   public:
    using Version = unsigned int;

    Repository(std::shared_ptr<IpfsDatastore> ipldStore,
               std::shared_ptr<KeyStore> keystore,
               std::shared_ptr<Config> config);

    virtual ~Repository() = default;

    /**
     * @brief Persistent data storage for small structured objects.
     * @return ipld storage
     */
    std::shared_ptr<IpfsDatastore> getIpldStore() const noexcept;

    /**
     * @brief Cryptoghraphy keys that are secret for filecoin node.
     * @return Keystore
     */
    std::shared_ptr<KeyStore> getKeyStore() const noexcept;

    /**
     * @brief User-editable configuration values.
     * @return Config
     */
    std::shared_ptr<Config> getConfig() const noexcept;

    /**
     * @brief A repo version is a single incrementing integer. All versions are
     * considered non-compatible.
     * @return version number
     */
    virtual fc::outcome::result<Version> getVersion() const = 0;
    outcome::result<FsStat> getStat(const std::string &path) override;
    outcome::result<uint64_t> getDiskUsage(const std::string &path) override;

   protected:
    /**
     * Load config
     * @param filename - full path to config file
     * @return error code in case of failure
     */
    outcome::result<void> loadConfig(const std::string &filename);
    outcome::result<StorageConfig> storageFromFile(
        const boost::filesystem::path &path);
    outcome::result<void> writeStorage(const boost::filesystem::path &path,
                                       StorageConfig config);

   private:
    std::shared_ptr<IpfsDatastore> ipld_store_;
    std::shared_ptr<KeyStore> keystore_;
    std::shared_ptr<Config> config_;
  };

}  // namespace fc::storage::repository

#endif  // FILECOIN_CORE_STORAGE_REPOSITORY_HPP
