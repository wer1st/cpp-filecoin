/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CPP_FILECOIN_SYNC_TIPSET_LOADER_HPP
#define CPP_FILECOIN_SYNC_TIPSET_LOADER_HPP

#include <set>

#include "block_loader.hpp"
#include "common.hpp"
#include "primitives/tipset/tipset.hpp"

namespace fc::sync {

  class TipsetLoader : public std::enable_shared_from_this<TipsetLoader> {
   public:
    /// Called when all tipset subobjects are available
    /// or tipset appeared to be bad
    using OnTipset = std::function<void(TipsetHash hash,
                                        outcome::result<TipsetCPtr> tipset)>;

    TipsetLoader(std::shared_ptr<libp2p::protocol::Scheduler> scheduler,
                 std::shared_ptr<BlockLoader> block_loader);

    void init(OnTipset callback);

    outcome::result<void> loadTipsetAsync(
        const TipsetKey &key,
        boost::optional<PeerId> preferred_peer,
        uint64_t depth = 1);

   private:
    void onBlock(const CID &cid, outcome::result<BlockHeader> bh);

    using Wantlist = std::set<CID>;

    struct RequestCtx {
      TipsetLoader &owner;

      TipsetKey tipset_key;

      // block cids we are waiting for
      Wantlist wantlist;

      // the puzzle being filled
      std::vector<boost::optional<BlockHeader>> blocks_filled;

      bool is_bad_tipset = false;

      libp2p::protocol::scheduler::Handle call_completed;

      RequestCtx(TipsetLoader &o, const TipsetKey &key);

      void onBlockSynced(const CID &cid, const BlockHeader &bh);

      void onError(const CID &cid);
    };

    void onRequestCompleted(TipsetHash hash,
                            outcome::result<TipsetCPtr> tipset);

    std::shared_ptr<libp2p::protocol::Scheduler> scheduler_;
    std::shared_ptr<BlockLoader> block_loader_;
    OnTipset callback_;
    std::map<TipsetHash, RequestCtx> tipset_requests_;
    Wantlist global_wantlist_;
    bool initialized_ = false;

    // friendship with contained objects is ok
    friend RequestCtx;
  };

}  // namespace fc::sync

#endif  // CPP_FILECOIN_SYNC_TIPSET_LOADER_HPP