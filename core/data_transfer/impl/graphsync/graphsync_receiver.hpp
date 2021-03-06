/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CPP_FILECOIN_DATA_TRANSFER_GRAPHSYNC_RECEIVER_HPP
#define CPP_FILECOIN_DATA_TRANSFER_GRAPHSYNC_RECEIVER_HPP

#include "data_transfer/message_receiver.hpp"

#include "common/logger.hpp"
#include "data_transfer/impl/graphsync/graphsync_manager.hpp"
#include "data_transfer/impl/libp2p_data_transfer_network.hpp"
#include "storage/ipfs/graphsync/graphsync.hpp"

namespace fc::data_transfer {

  using storage::ipfs::graphsync::Graphsync;

  class GraphsyncReceiver : public MessageReceiver {
   public:
    GraphsyncReceiver(std::weak_ptr<DataTransferNetwork> network,
                      std::shared_ptr<Graphsync> graphsync,
                      std::weak_ptr<Manager> graphsync_manager,
                      PeerInfo peer);

    outcome::result<void> receiveRequest(
        const PeerInfo &initiator, const DataTransferRequest &request) override;

    outcome::result<void> receiveResponse(
        const PeerInfo &sender, const DataTransferResponse &response) override;

    void receiveError() override;

    outcome::result<void> sendResponse(const PeerInfo &peer,
                                       bool is_accepted,
                                       const TransferId &transfer_id);

    void subscribeToEvents(std::weak_ptr<Subscriber> subscriber);

    void unsubscribe(std::weak_ptr<Subscriber> subscriber);

    /**
     * Assembles a graphsync request and determines if the transfer was
     * completed/successful. Notifies subscribers of final request status
     * @return
     */
    outcome::result<void> sendGraphSyncRequest(
        const PeerInfo &initiator,
        const TransferId &transfer_id,
        bool is_pull,
        const PeerInfo &sender,
        const CID &root,
        DataTransferMessage message,
        gsl::span<const uint8_t> selector);

    void notifySubscribers(const Event &event,
                           const ChannelState &channel_state);

    std::weak_ptr<DataTransferNetwork> network_;
    std::shared_ptr<Graphsync> graphsync_;
    std::weak_ptr<Manager> graphsync_manager_;
    PeerInfo peer_;
    std::vector<std::weak_ptr<Subscriber>> subscribers_;
    common::Logger logger_ = common::createLogger("GraphsyncReceiver");
  };

}  // namespace fc::data_transfer

#endif  // CPP_FILECOIN_DATA_TRANSFER_GRAPHSYNC_RECEIVER_HPP
