/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "miner/storage_fsm/impl/sealing_impl.hpp"

#include "common/bitsutil.hpp"
#include "vm/actor/builtin/miner/miner_actor.hpp"

#define FSM_SEND(info, event) OUTCOME_EXCEPT(fsm_->send(info, event))

#define CALLBACK_ACTION(_action)                      \
  [this](auto info, auto event, auto from, auto to) { \
    logger_->debug("Storage FSM " #_action);          \
    _action(info, event, from, to);                   \
  }

namespace fc::mining {

  uint64_t getDealPerSectorLimit(SectorSize size) {
    if (size < (uint64_t(64) << 30)) {
      return 256;
    }
    return 512;
  }

  outcome::result<void> SealingImpl::run() {
    // TODO: restart fsm
    return outcome::success();
  }

  void SealingImpl::stop() {
    // TODO: log it
    fsm_->stop();
  }

  outcome::result<PieceAttributes> SealingImpl::addPieceToAnySector(
      UnpaddedPieceSize size, const PieceData &piece_data, DealInfo deal) {
    // TODO: Log it

    if (primitives::piece::paddedSize(size) != size) {
      return outcome::success();  // TODO: error
    }

    auto sector_size = sealer_->getSectorSize();
    if (size > PaddedPieceSize(sector_size).unpadded()) {
      return outcome::success();  // TODO: error
    }

    bool is_start_packing = false;
    PieceAttributes piece;
    piece.size = size;

    {
      std::unique_lock lock(unsealed_mutex_);
      OUTCOME_TRY(sector_and_padding, getSectorAndPadding(size));

      piece.sector = sector_and_padding.sector;

      PieceData zero_file("/dev/zero");
      for (const auto &pad : sector_and_padding.pads) {
        OUTCOME_TRY(addPiece(
            sector_and_padding.sector, pad.unpadded(), zero_file, boost::none));
      }

      piece.offset = unsealed_sectors_[sector_and_padding.sector].stored;

      OUTCOME_TRY(addPiece(sector_and_padding.sector, size, piece_data, deal));

      is_start_packing =
          unsealed_sectors_[sector_and_padding.sector].deals_number
          >= getDealPerSectorLimit(sector_size);
    }

    if (is_start_packing) {
      OUTCOME_TRY(startPacking(piece.sector));
    }

    return std::move(piece);
  }

  outcome::result<void> SealingImpl::remove(SectorNumber sector_id) {
    // TODO: send fsm event
    return outcome::success();
  }

  Address SealingImpl::getAddress() const {
    return miner_address_;
  }

  std::vector<SectorNumber> SealingImpl::getListSectors() const {
    std::vector<SectorNumber> keys = {};
    for (const auto &[key, value] : sectors_) {
      keys.push_back(key);
    }
    return keys;
  }

  outcome::result<std::shared_ptr<SectorInfo>> SealingImpl::getSectorInfo(
      SectorNumber id) const {
    const auto &maybe_sector{sectors_.find(id)};
    if (maybe_sector == sectors_.cend()) {
      return outcome::success();  // TODO: error
    }
    return maybe_sector->second;
  }

  outcome::result<void> SealingImpl::forceSectorState(SectorNumber id,
                                                      SealingState state) {
    // TODO: send fsm event
    return outcome::success();
  }

  outcome::result<void> SealingImpl::markForUpgrade(SectorNumber id) {
    std::unique_lock lock(upgrade_mutex_);

    // TODO: Check current state

    return outcome::success();
  }

  bool SealingImpl::isMarkedForUpgrade(SectorNumber id) {
    std::shared_lock lock(upgrade_mutex_);
    return to_upgrade_.find(id) != to_upgrade_.end();
  }

  outcome::result<void> SealingImpl::startPacking(SectorNumber id) {
    // TODO: log it
    auto sector_info = sectors_.find(id);
    if (sector_info == sectors_.end()) {
      return outcome::success();  // TODO: ERROR
    }

    OUTCOME_TRY(fsm_->send(sector_info->second, SealingEvent::kIncoming));

    {
      std::lock_guard lock(unsealed_mutex_);
      unsealed_sectors_.erase(id);
    }

    return outcome::success();
  }

  outcome::result<SealingImpl::SectorPaddingResponse>
  SealingImpl::getSectorAndPadding(UnpaddedPieceSize size) {
    auto sector_size = sealer_->getSectorSize();

    for (const auto &[key, value] : unsealed_sectors_) {
      auto pads =
          proofs::Proofs::GetRequiredPadding(value.stored, size.padded());
      if (value.stored + size.padded() + pads.size < sector_size) {
        return SectorPaddingResponse{
            .sector = key,
            .pads = std::move(pads.pads),
        };
      }
    }

    OUTCOME_TRY(new_sector, newDealSector());

    unsealed_sectors_[new_sector] = UnsealedSectorInfo{
        .deals_number = 0,
        .stored = PaddedPieceSize(0),
        .piece_sizes = {},
    };

    return SectorPaddingResponse{
        .sector = new_sector,
        .pads = {},
    };
  }

  outcome::result<void> SealingImpl::addPiece(SectorNumber sector_id,
                                              UnpaddedPieceSize size,
                                              const PieceData &piece,
                                              boost::optional<DealInfo> deal) {
    // TODO: log it
    OUTCOME_TRY(piece_info,
                sealer_->addPiece(minerSector(sector_id),
                                  unsealed_sectors_[sector_id].piece_sizes,
                                  size,
                                  piece));

    Piece new_piece{
        .piece = piece_info,
        .deal_info = deal,
    };

    auto info = sectors_[sector_id];
    info->pieces.push_back(new_piece);
    OUTCOME_TRY(fsm_->send(info, SealingEvent::kWaitDeals));

    auto unsealed_info = unsealed_sectors_.find(sector_id);
    if (deal) {
      unsealed_info->second.deals_number++;
    }
    unsealed_info->second.stored += new_piece.piece.size;
    unsealed_info->second.piece_sizes.push_back(
        new_piece.piece.size.unpadded());

    return outcome::success();
  }

  outcome::result<SectorNumber> SealingImpl::newDealSector() {
    // TODO: checks available sectors

    // TODO: log it
    auto sector_id = 0;  // TODO: Next counter

    auto sector = std::make_shared<SectorInfo>();
    sector->sector_number = sector_id;
    OUTCOME_TRYA(sector->sector_type,
                 primitives::sector::sealProofTypeFromSectorSize(
                     sealer_->getSectorSize()));
    OUTCOME_TRY(fsm_->begin(sector, SealingState::kStateUnknown));
    sectors_[sector_id] = sector;

    // TODO: Timer for start packing

    return sector_id;
  }

  std::vector<UnpaddedPieceSize> filler(UnpaddedPieceSize in) {
    uint64_t to_fill = in.padded();

    uint64_t pieces_size = common::countSetBits(to_fill);

    std::vector<UnpaddedPieceSize> out;
    for (size_t i = 0; i < pieces_size; ++i) {
      uint64_t next = common::countTrailingZeros(to_fill);
      uint64_t piece_size = uint64_t(1) << next;

      to_fill ^= piece_size;

      out.push_back(PaddedPieceSize(piece_size).unpadded());
    }
    return out;
  }

  std::vector<SealingTransition> SealingImpl::makeFSMTransitions() {
    return {
        // Main pipeline
        SealingTransition(SealingEvent::kWaitDeals)
            .fromMany(SealingState::kWaitDeals, SealingState::kStateUnknown)
            .to(SealingState::kWaitDeals)
            .action(CALLBACK_ACTION(
                [this](auto info, auto event, auto from, auto to) {
                  logger_->info("Waiting for deals {}", info->sector_number);
                })),
        SealingTransition(SealingEvent::kIncoming)
            .fromMany(SealingState::kStateUnknown, SealingState::kWaitSeed)
            .to(SealingState::kPacking)
            .action(CALLBACK_ACTION(onIncoming)),
        SealingTransition(SealingEvent::kPreCommit1)
            .fromMany(SealingState::kPacking,
                      SealingState::kSealPreCommit1Fail,
                      SealingState::kSealPreCommit2Fail)
            .to(SealingState::kPreCommit1)
            .action(CALLBACK_ACTION(onPreCommit1)),
        SealingTransition(SealingEvent::kPreCommit2)
            .fromMany(SealingState::kPreCommit1,
                      SealingState::kSealPreCommit2Fail)
            .to(SealingState::kPreCommit2)
            .action(CALLBACK_ACTION(onPreCommit2)),
        SealingTransition(SealingEvent::kPreCommit)
            .fromMany(SealingState::kPreCommit2, SealingState::kPreCommitFail)
            .to(SealingState::kPreCommitting)
            .action(CALLBACK_ACTION(onPreCommit)),
        SealingTransition(SealingEvent::kPreCommitWait)
            .from(SealingState::kPreCommitting)
            .to(SealingState::kPreCommittingWait)
            .action(CALLBACK_ACTION(onPreCommitWaiting)),
        SealingTransition(SealingEvent::kWaitSeed)
            .fromMany(SealingState::kPreCommitting,
                      SealingState::kPreCommittingWait,
                      SealingState::kCommitFail)
            .to(SealingState::kWaitSeed)
            .action(CALLBACK_ACTION(onWaitSeed)),
        SealingTransition(SealingEvent::kCommit)
            .fromMany(SealingState::kWaitSeed,
                      SealingState::kCommitFail,
                      SealingState::kComputeProofFail)
            .to(SealingState::kCommitting)
            .action(CALLBACK_ACTION(onCommit)),
        SealingTransition(SealingEvent::kCommitWait)
            .from(SealingState::kCommitting)
            .to(SealingState::kCommitWait)
            .action(CALLBACK_ACTION(onCommitWait)),
        SealingTransition(SealingEvent::kFinalizeSector)
            .fromMany(SealingState::kCommitWait, SealingState::kFinalizeFail)
            .to(SealingState::kFinalizeSector)
            .action(CALLBACK_ACTION(onFinalize)),
        SealingTransition(SealingEvent::kProve)
            .from(SealingState::kFinalizeSector)
            .to(SealingState::kProving)
            .action(CALLBACK_ACTION(
                [this](auto info, auto event, auto from, auto to) {
                  logger_->info("Proving sector {}", info->sector_number);
                })),

        // Errors
        SealingTransition(SealingEvent::kSealPreCommit1Failed)
            .fromMany(SealingState::kPreCommit1,
                      SealingState::kPreCommitting,
                      SealingState::kCommitFail)
            .to(SealingState::kSealPreCommit1Fail)
            .action(CALLBACK_ACTION(onSealPreCommit1Failed)),
        SealingTransition(SealingEvent::kSealPreCommit2Failed)
            .from(SealingState::kPreCommit2)
            .to(SealingState::kSealPreCommit2Fail)
            .action(CALLBACK_ACTION(onSealPreCommit2Failed)),
        SealingTransition(SealingEvent::kPreCommitFailed)
            .fromMany(SealingState::kPreCommitting,
                      SealingState::kPreCommittingWait,
                      SealingState::kWaitSeed)
            .to(SealingState::kPreCommitFail)
            .action(CALLBACK_ACTION(onPreCommitFailed)),
        SealingTransition(SealingEvent::kComputeProofFailed)
            .from(SealingState::kCommitting)
            .to(SealingState::kComputeProofFail)
            .action(CALLBACK_ACTION(onComputeProofFailed)),
        SealingTransition(SealingEvent::kCommitFailed)
            .fromMany(SealingState::kCommitting, SealingState::kCommitWait)
            .to(SealingState::kCommitFail)
            .action(CALLBACK_ACTION(onCommitFailed)),
        SealingTransition(SealingEvent::kFinalizeFailed)
            .from(SealingState::kFinalizeSector)
            .to(SealingState::kFinalizeFail)
            .action(CALLBACK_ACTION(onFinalizeFailed)),

        // Post-seal
        SealingTransition(SealingEvent::kFault)
            .from(SealingState::kProving)
            .to(SealingState::kFaulty),
        SealingTransition(SealingEvent::kFaultReport)
            .fromMany(SealingState::kFaulty, SealingState::kProving)
            .to(SealingState::kFaultReported),
        SealingTransition(SealingEvent::kRemove)
            .from(SealingState::kProving)
            .to(SealingState::kRemoving),
        SealingTransition(SealingEvent::kRemoveFailed)
            .from(SealingState::kRemoving)
            .to(SealingState::kRemoveFail),
        SealingTransition(SealingEvent::kRemoveFinal)
            .from(SealingState::kRemoving)
            .to(SealingState::kRemoved),
    };
  }

  void SealingImpl::onIncoming(const std::shared_ptr<SectorInfo> &info,
                               SealingEvent event,
                               SealingState from,
                               SealingState to) {
    logger_->info("Performing filling up rest of the sector",
                  info->sector_number);

    UnpaddedPieceSize allocated(0);
    for (const auto &piece : info->pieces) {
      allocated += piece.piece.size.unpadded();
    }

    auto ubytes = PaddedPieceSize(sealer_->getSectorSize()).unpadded();

    if (allocated > ubytes) {
      logger_->error("too much data in sector: {} > {}", allocated, ubytes);
      return;
    }

    auto filler_sizes = filler(UnpaddedPieceSize(ubytes - allocated));

    if (!filler_sizes.empty()) {
      logger_->warn("Creating {} filler pieces for sector {}",
                    filler_sizes.size(),
                    info->sector_number);
    }

    auto maybe_result = pledgeSector(minerSector(info->sector_number),
                                     info->getExistingPieceSizes(),
                                     filler_sizes);

    if (maybe_result.has_error()) {
      logger_->error("Pledge sector error: {}", maybe_result.error().message());
      return;
    }

    for (const auto &new_piece : maybe_result.value()) {
      info->pieces.push_back(
          Piece{.piece = new_piece, .deal_info = boost::none});
    }

    OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kPreCommit1))
  }

  outcome::result<std::vector<PieceInfo>> SealingImpl::pledgeSector(
      SectorId sector,
      std::vector<UnpaddedPieceSize> existing_piece_sizes,
      gsl::span<const UnpaddedPieceSize> sizes) {
    if (sizes.empty()) {
      return outcome::success();
    }

    std::string existing_piece_str = "empty";
    if (!existing_piece_sizes.empty()) {
      existing_piece_str = std::to_string(existing_piece_sizes[0]);
      for (size_t i = 1; i < existing_piece_sizes.size(); ++i) {
        existing_piece_str += ", ";
        existing_piece_str += std::to_string(existing_piece_sizes[i]);
      }
    }

    logger_->info("Pledge " + primitives::sector_file::sectorName(sector)
                  + ", contains " + existing_piece_str);

    std::vector<PieceInfo> result;

    PieceData zero_file("/dev/zero");
    for (const auto &size : sizes) {
      OUTCOME_TRY(
          piece_info,
          sealer_->addPiece(sector, existing_piece_sizes, size, zero_file));

      existing_piece_sizes.push_back(size);

      result.push_back(piece_info);
    }

    return std::move(result);
  }

  SectorId SealingImpl::minerSector(SectorNumber num) {
    auto miner_id = miner_address_.getId();

    return SectorId{
        .miner = miner_id,
        .sector = num,
    };
  }

  void SealingImpl::onPreCommit1(const std::shared_ptr<SectorInfo> &info,
                                 SealingEvent event,
                                 SealingState from,
                                 SealingState to) {
    // TODO: check Packing

    logger_->info("Performing {} sector replication", info->sector_number);

    auto maybe_ticket = getTicket(info);
    if (maybe_ticket.has_error()) {
      logger_->error("Get ticket error: {}", maybe_ticket.error().message());
      OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kSealPreCommit1Failed))
      return;
    }

    // TODO: add check priority
    auto maybe_result =
        sealer_->sealPreCommit1(minerSector(info->sector_number),
                                maybe_ticket.value().ticket,
                                info->getPieceInfos());

    if (maybe_result.has_error()) {
      logger_->error("Seal pre commit 1 error: {}",
                     maybe_result.error().message());
      OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kSealPreCommit1Failed))
      return;
    }

    info->precommit1_output = maybe_result.value();
    info->ticket = maybe_ticket.value().ticket;
    info->ticket_epoch = maybe_ticket.value().epoch;
    info->precommit2_fails = 0;

    OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kPreCommit2))
  }

  void SealingImpl::onPreCommit2(const std::shared_ptr<SectorInfo> &info,
                                 SealingEvent event,
                                 SealingState from,
                                 SealingState to) {
    // TODO: add check priority
    auto maybe_cid = sealer_->sealPreCommit2(minerSector(info->sector_number),
                                             info->precommit1_output);
    if (maybe_cid.has_error()) {
      logger_->error("Seal pre commit 2 error: {}",
                     maybe_cid.error().message());
      OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kSealPreCommit2Failed))
      return;
    }

    info->comm_d = maybe_cid.value().unsealed_cid;
    info->comm_r = maybe_cid.value().sealed_cid;

    OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kPreCommit))
  }

  void SealingImpl::onPreCommit(const std::shared_ptr<SectorInfo> &info,
                                SealingEvent event,
                                SealingState from,
                                SealingState to) {
    auto maybe_head = api_->ChainHead();
    if (maybe_head.has_error()) {
      logger_->error("handlePreCommitting: api error, not proceeding: {}",
                     maybe_head.error().message());
      return;
    }

    auto maybe_key = maybe_head.value().makeKey();
    if (maybe_key.has_error()) {
      logger_->error("cannot make key from tipset: {}",
                     maybe_key.error().message());
      return;
    }

    auto maybe_worker_addr =
        api_->StateMinerWorker(miner_address_, maybe_key.value());
    if (maybe_worker_addr.has_error()) {
      logger_->error("handlePreCommitting: api error, not proceeding: {}",
                     maybe_worker_addr.error().message());
      return;
    }

    // TODO: check Precommit

    // TODO: check Policy

    // TODO: CBOR params

    logger_->info("submitting precommit for sector: {}", info->sector_number);

    auto maybe_signed_msg = api_->MpoolPushMessage(vm::message::UnsignedMessage(
        maybe_worker_addr.value(),
        miner_address_,
        0,
        0,
        1,
        1000000,
        vm::actor::builtin::miner::PreCommitSector::Number,
        {}));

    if (maybe_signed_msg.has_error()) {
      logger_->error("pushing message to mpool: {}",
                     maybe_signed_msg.error().message());
      OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kPreCommitFailed))
      return;
    }

    info->precommit_message = maybe_signed_msg.value().getCid();

    OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kPreCommitWait))
  }

  void SealingImpl::onPreCommitWaiting(const std::shared_ptr<SectorInfo> &info,
                                       SealingEvent event,
                                       SealingState from,
                                       SealingState to) {
    if (!info->precommit_message) {
      logger_->error("precommit message was nil");
      OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kPreCommitFailed))
      return;
    }

    logger_->info("Sector precommitted: {}", info->sector_number);
    auto maybe_channel = api_->StateWaitMsg(info->precommit_message.value());
    if (maybe_channel.has_error()) {
      logger_->error("get message failed: {}", maybe_channel.error().message());
      return;
    }

    auto maybe_lookup = maybe_channel.value().waitSync();
    if (maybe_lookup.has_error()) {
      logger_->error("sector precommit failed: {}",
                     maybe_lookup.error().message());
      OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kPreCommitFailed))
      return;
    }

    if (maybe_lookup.value().receipt.exit_code != vm::VMExitCode::kOk) {
      logger_->error("sector precommit failed: exit code is {}",
                     maybe_lookup.value().receipt.exit_code);
      OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kPreCommitFailed))
      return;
    }

    auto maybe_tipset_key = maybe_lookup.value().tipset.makeKey();

    if (maybe_tipset_key.has_error()) {
      logger_->error("tipset make key error: {}",
                     maybe_tipset_key.error().message());
      return;
    }

    info->precommit_tipset = maybe_tipset_key.value();

    OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kWaitSeed))
  }

  void SealingImpl::onWaitSeed(const std::shared_ptr<SectorInfo> &info,
                               SealingEvent event,
                               SealingState from,
                               SealingState to) {
    // TODO: get pre commit info from api

    auto random_height = vm::actor::builtin::miner::
        kPreCommitChallengeDelay;  // TODO: add height from pre commit info

    auto maybe_error = events_->chainAt(
        [=](const Tipset &tipset,
            ChainEpoch current_height) -> outcome::result<void> {
          // TODO: CBOR miner address

          OUTCOME_TRY(tipset_key, tipset.makeKey());

          auto maybe_randomness =
              api_->ChainGetRandomness(tipset_key,
                                       crypto::randomness::DomainSeparationTag::
                                           InteractiveSealChallengeSeed,
                                       random_height,
                                       {});
          if (maybe_randomness.has_error()) {
            logger_->error(
                "failed to get randomness for computing seal proof (curHeight "
                "{}; randHeight {}): {}",
                current_height,
                random_height,
                maybe_randomness.error().message());
            OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kPreCommitFailed))
            return maybe_randomness.error();
          }

          info->seed = maybe_randomness.value();
          info->seed_epoch = random_height;

          OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kCommit))
          return outcome::success();
        },
        [=](const Tipset &token) -> outcome::result<void> {
          logger_->warn("revert in interactive commit sector step");
          // TODO: cancel running and restart
          return outcome::success();
        },
        kInteractivePoRepConfidence,
        random_height);

    if (maybe_error.has_error()) {
      logger_->warn("waitForPreCommitMessage ChainAt errored: {}",
                    maybe_error.error().message());
    }
  }

  void SealingImpl::onCommit(const std::shared_ptr<SectorInfo> &info,
                             SealingEvent event,
                             SealingState from,
                             SealingState to) {
    logger_->info("scheduling seal proof computation...");

    logger_->info(
        "commit {} sector; ticket(epoch): {}({});"
        "seed(epoch): {}({}); ticket(epoch): {}({}); comm_r: {}; comm_d: {}",
        info->sector_number,
        info->ticket,
        info->ticket_epoch,
        info->seed,
        info->seed_epoch,
        info->comm_r,
        info->comm_d);

    sector_storage::SectorCids cids{
        .sealed_cid = info->comm_r,
        .unsealed_cid = info->comm_d,
    };

    // TODO: add check priority
    auto maybe_commit_1_output =
        sealer_->sealCommit1(minerSector(info->sector_number),
                             info->ticket,
                             info->seed,
                             info->getPieceInfos(),
                             cids);
    if (maybe_commit_1_output.has_error()) {
      logger_->error("computing seal proof failed(1): {}",
                     maybe_commit_1_output.error().message());
      OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kComputeProofFailed));
      return;
    }

    auto maybe_proof = sealer_->sealCommit2(minerSector(info->sector_number),
                                            maybe_commit_1_output.value());
    if (maybe_proof.has_error()) {
      logger_->error("computing seal proof failed(2): {}",
                     maybe_proof.error().message());
      OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kComputeProofFailed));
      return;
    }

    auto maybe_head = api_->ChainHead();
    if (maybe_head.has_error()) {
      logger_->error("handleCommitting: api error, not proceeding: {}",
                     maybe_head.error().message());
      return;
    }

    // TODO: check Commit

    // TODO: maybe split into 2 states here

    // TODO: CBOR params

    auto maybe_tipset_key = maybe_head.value().makeKey();

    if (maybe_tipset_key.has_error()) {
      logger_->error("make tipset key error: {}",
                     maybe_tipset_key.error().message());
      return;
    }

    auto maybe_worker_addr =
        api_->StateMinerWorker(miner_address_, maybe_tipset_key.value());
    if (maybe_worker_addr.has_error()) {
      logger_->error("handleCommitting: api error, not proceeding: {}",
                     maybe_worker_addr.error().message());
      return;
    }

    auto maybe_collateral = api_->StateMinerInitialPledgeCollateral(
        miner_address_, info->sector_number, maybe_tipset_key.value());
    if (maybe_collateral.has_error()) {
      logger_->error("getting initial pledge collateral: {}",
                     maybe_collateral.error().message());
      return;
    }

    auto maybe_signed_msg = api_->MpoolPushMessage(vm::message::UnsignedMessage(
        maybe_worker_addr.value(),
        miner_address_,
        0,
        maybe_collateral.value(),
        1,
        1000000,
        vm::actor::builtin::miner::ProveCommitSector::Number,
        {}));

    if (maybe_signed_msg.has_error()) {
      logger_->error("pushing message to mpool: {}",
                     maybe_signed_msg.error().message());
      OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kCommitFailed));
      return;
    }

    info->proof = maybe_proof.value();
    info->message = maybe_signed_msg.value().getCid();
    OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kCommitWait));
  }

  void SealingImpl::onCommitWait(const std::shared_ptr<SectorInfo> &info,
                                 SealingEvent event,
                                 SealingState from,
                                 SealingState to) {
    if (!info->message) {
      logger_->error(
          "sector {} entered commit wait state without a message cid",
          info->sector_number);
      OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kCommitFailed))
      return;
    }

    auto maybe_channel = api_->StateWaitMsg(info->message.get());

    if (maybe_channel.has_error()) {
      logger_->error("get message failed: {}", maybe_channel.error().message());
      return;
    }

    auto maybe_message_lookup = maybe_channel.value().waitSync();

    if (maybe_message_lookup.has_error()) {
      logger_->error("failed to wait for porep inclusion: {}",
                     maybe_message_lookup.error().message());
      OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kCommitFailed))
      return;
    }

    if (maybe_message_lookup.value().receipt.exit_code != vm::VMExitCode::kOk) {
      logger_->error(
          "submitting sector proof failed with code {}, message cid: {}",
          maybe_message_lookup.value().receipt.exit_code,
          info->message.get());
      OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kCommitFailed))
      return;
    }

    auto maybe_tipset_key = maybe_message_lookup.value().tipset.makeKey();

    if (maybe_tipset_key.has_error()) {
      logger_->error("make tipset key error: {}",
                     maybe_tipset_key.error().message());
      return;
    }

    auto maybe_error = api_->StateSectorGetInfo(
        miner_address_, info->sector_number, maybe_tipset_key.value());

    if (maybe_error.has_error()) {
      logger_->error(
          "proof validation failed, sector not found in sector set after cron: "
          "{}",
          maybe_error.error().message());
      OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kCommitFailed))
      return;
    }

    OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kFinalizeSector))
  }

  void SealingImpl::onFinalize(const std::shared_ptr<SectorInfo> &info,
                               SealingEvent event,
                               SealingState from,
                               SealingState to) {
    // TODO: Maybe wait for some finality

    auto maybe_error =
        sealer_->finalizeSector(minerSector(info->sector_number));
    if (maybe_error.has_error()) {
      logger_->error("finalize sector: {}", maybe_error.error().message());
      OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kFinalizeFailed));
      return;
    }

    OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kProve))
  }

  void SealingImpl::onSealPreCommit1Failed(
      const std::shared_ptr<SectorInfo> &info,
      SealingEvent event,
      SealingState from,
      SealingState to) {
    info->invalid_proofs = 0;
    info->precommit2_fails = 0;

    // TODO: wait some time

    OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kPreCommit1));
  }

  void SealingImpl::onSealPreCommit2Failed(
      const std::shared_ptr<SectorInfo> &info,
      SealingEvent event,
      SealingState from,
      SealingState to) {
    info->invalid_proofs = 0;
    info->precommit2_fails++;

    // TODO: wait some time

    if (info->precommit2_fails > 1) {
      OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kPreCommit1));
      return;
    }

    OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kPreCommit2));
  }

  void SealingImpl::onPreCommitFailed(const std::shared_ptr<SectorInfo> &info,
                                      SealingEvent event,
                                      SealingState from,
                                      SealingState to) {
    auto maybe_head = api_->ChainHead();
    if (maybe_head.has_error()) {
      logger_->error("handlePreCommitFailed: api error, not proceeding: {}",
                     maybe_head.error().message());
      return;
    }

    // TODO: check precommit

    // TODO: check precommitted

    if (info->precommit_message) {
      logger_->warn(
          "retrying precommit even though the message failed to apply");
    }

    // TODO: wait some time

    OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kPreCommit));
  }

  void SealingImpl::onComputeProofFailed(
      const std::shared_ptr<SectorInfo> &info,
      SealingEvent event,
      SealingState from,
      SealingState to) {
    // TODO: Check sector files

    // TODO: wait some time

    if (info->invalid_proofs > 1) {
      logger_->error("consecutive compute fails");
      OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kSealPreCommit1Failed))
      return;
    }

    info->invalid_proofs++;

    OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kCommit))
  }

  void SealingImpl::onCommitFailed(const std::shared_ptr<SectorInfo> &info,
                                   SealingEvent event,
                                   SealingState from,
                                   SealingState to) {
    auto maybe_head = api_->ChainHead();
    if (maybe_head.has_error()) {
      logger_->error("handleCommitting: api error, not proceeding: {}",
                     maybe_head.error().message());
      return;
    }

    // TODO: check precommit

    // TODO: check commit

    // TODO: Check sector files

    // TODO: wait some time

    info->invalid_proofs++;

    OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kCommit))
  }

  void SealingImpl::onFinalizeFailed(const std::shared_ptr<SectorInfo> &info,
                                     SealingEvent event,
                                     SealingState from,
                                     SealingState to) {
    // TODO: Check sector files

    // TODO: wait some type

    OUTCOME_EXCEPT(fsm_->send(info, SealingEvent::kFinalizeSector));
  }

  outcome::result<SealingImpl::TicketInfo> SealingImpl::getTicket(
      const std::shared_ptr<SectorInfo> &info) {
    OUTCOME_TRY(head, api_->ChainHead());

    OUTCOME_TRY(tipset_key, head.makeKey());

    ChainEpoch ticket_epoch =
        head.height - vm::actor::builtin::miner::kChainFinalityish;

    // TODO: get precommit info from api

    // TODO: Marshal param

    OUTCOME_TRY(
        randomness,
        api_->ChainGetRandomness(tipset_key,
                                 api::DomainSeparationTag::SealRandomness,
                                 ticket_epoch,
                                 {}));

    return TicketInfo{
        .ticket = randomness,
        .epoch = ticket_epoch,
    };
  }
}  // namespace fc::mining