/*
 * Copyright (c) 2024 REDS-HEIG-VD and ESL-EPFL
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/* @file
 * Implementation of a bi-mode branch predictor
 */

#include "cpu/pred/gshare_replicated_bhr_inclusive.hh"

#include "base/bitfield.hh"
#include "base/intmath.hh"
#include "mem/cache/base.hh"

namespace gem5
{

namespace branch_prediction
{

GshareReplicatedInclusiveBP::GshareReplicatedInclusiveBP(const GshareReplicatedInclusiveBPParams &params)
    : ConditionalPredictor(params),
      globalHistoryRegExclusive(params.numThreads, 0),
      globalHistoryRegInclusive(params.numThreads, 0),
      globalHistoryBits(ceilLog2(params.global_predictor_size)),
      globalPredictorSize(params.global_predictor_size),
      globalCtrBits(params.global_counter_bits),
      globalCtrs(globalPredictorSize, SatCounter8(globalCtrBits)),
      icacheBlockShift(floorLog2(params.system->cacheLineSize())),
      stats(this)
{

    if (!isPowerOf2(globalPredictorSize)) {
        fatal("Invalid global history predictor size.\n");
    }
    historyRegisterMask = mask(globalHistoryBits);
    globalHistoryMask = globalPredictorSize - 1;
    takenThreshold = (1ULL << (globalCtrBits - 1)) - 1;
}

/*
 * For an unconditional branch we set its history such that
 * everything is set to taken. I.e., its choice predictor
 * chooses the taken array and the taken array predicts taken.
 */
void
GshareReplicatedInclusiveBP::uncondBranch(ThreadID tid, Addr pc, void *&bp_history)
{
    uint32_t icache_set = getIcacheSet(pc);
    bool oddSet = (icache_set & 1U) != 0;

    BPHistory *history = new BPHistory;
    history->globalHistoryReg = oddSet ? globalHistoryRegInclusive[tid]
                                       : globalHistoryRegExclusive[tid];
    history->finalPred = true;
    bp_history = static_cast<void *>(history);
}

void
GshareReplicatedInclusiveBP::updateHistories(ThreadID tid, Addr pc, bool uncond, bool taken,
                          Addr target, const StaticInstPtr &inst,
                          void *&bp_history)
{
    assert(uncond || bp_history);
    if (uncond) {
        uncondBranch(tid, pc, bp_history);
    }
    updateGlobalHistReg(tid, pc, taken);
}

void 
GshareReplicatedInclusiveBP::branchPlaceholder(ThreadID tid, Addr pc,
                                bool uncond, void * &bpHistory)
{
// Placeholder for a function that only returns history items
}

void
GshareReplicatedInclusiveBP::squash(ThreadID tid, void *&bp_history)
{
    BPHistory *history = static_cast<BPHistory *>(bp_history);
    if (history->oddSet)
        globalHistoryRegInclusive[tid] = history->globalHistoryReg;
    else {
        globalHistoryRegExclusive[tid] = history->globalHistoryReg;
        globalHistoryRegInclusive[tid] = history->globalHistoryRegInclusive;
    }

    delete history;
    bp_history = nullptr;
}

/*
 * Here we lookup the actual branch prediction. A hash of
 * the global history register and a branch's PC is used to
 * index into the counter, which both present a prediction.
 */
bool
GshareReplicatedInclusiveBP::lookup(ThreadID tid, Addr branchAddr, void *&bp_history)
{
    uint32_t icache_set = getIcacheSet(branchAddr);
    bool oddSet = (icache_set & 1U) != 0;

    unsigned selectedGhr;
    if (oddSet) {
        selectedGhr = globalHistoryRegInclusive[tid];
        stats.lookupUsedGhrInclusive++;
    } else {
        selectedGhr = globalHistoryRegExclusive[tid];
        stats.lookupUsedGhrExclusive++;
    }

    unsigned globalHistoryIdx =
        (((branchAddr >> instShiftAmt) ^ selectedGhr) &
         globalHistoryMask);

    assert(globalHistoryIdx < globalPredictorSize);

    bool final_prediction = globalCtrs[globalHistoryIdx] > takenThreshold;

    BPHistory *history = new BPHistory;
    history->globalHistoryReg = selectedGhr;
    history->globalHistoryRegInclusive = globalHistoryRegInclusive[tid];
    history->finalPred = final_prediction;
    bp_history = static_cast<void *>(history);

    return final_prediction;
}

/* Updates the counter values based on the actual branch
 * direction.
 */
void
GshareReplicatedInclusiveBP::update(ThreadID tid, Addr branchAddr, bool taken,
                 void *&bp_history, bool squashed,
                 const StaticInstPtr &inst, Addr target)
{
    uint32_t icache_set = getIcacheSet(branchAddr);
    bool oddSet = (icache_set & 1U) != 0;

    assert(bp_history);

    BPHistory *history = static_cast<BPHistory *>(bp_history);

    // We do not update the counters speculatively on a squash.
    // We just restore the global history register.
    if (squashed) {
        if (oddSet) {
            globalHistoryRegInclusive[tid] = (history->globalHistoryReg << 1) | taken;
        }
        else {
            globalHistoryRegExclusive[tid] = (history->globalHistoryReg << 1) | taken;
            globalHistoryRegInclusive[tid] = (history->globalHistoryRegInclusive << 1) | taken;
        }
        return;
    }

    unsigned globalHistoryIdx =
        (((branchAddr >> instShiftAmt) ^ history->globalHistoryReg) &
         globalHistoryMask);

    assert(globalHistoryIdx < globalPredictorSize);

    if (taken) {
        globalCtrs[globalHistoryIdx]++;
    } else {
        globalCtrs[globalHistoryIdx]--;
    }
    delete history;
    bp_history = nullptr;
}

void
GshareReplicatedInclusiveBP::updateGlobalHistReg(ThreadID tid, Addr branchAddr, bool taken)
{
    uint32_t icache_set = getIcacheSet(branchAddr);
    bool oddSet = (icache_set & 1U) != 0;

    if (oddSet) {
        unsigned selectedGhr = taken ? (globalHistoryRegInclusive[tid] << 1) | 1
                                     : (globalHistoryRegInclusive[tid] << 1);
        selectedGhr &= historyRegisterMask;
        globalHistoryRegInclusive[tid] = selectedGhr;
        stats.updateUsedGhrInclusive++;
    }
    else {
        unsigned selectedGhr = taken ? (globalHistoryRegExclusive[tid] << 1) | 1
                                     : (globalHistoryRegExclusive[tid] << 1);
        selectedGhr &= historyRegisterMask;
        globalHistoryRegExclusive[tid] = selectedGhr;
        stats.updateUsedGhrExclusive++;

        selectedGhr = taken ? (globalHistoryRegInclusive[tid] << 1) | 1
                            : (globalHistoryRegInclusive[tid] << 1);
        selectedGhr &= historyRegisterMask;
        globalHistoryRegInclusive[tid] = selectedGhr;
        stats.updateUsedGhrInclusive++;
    }
}

GshareReplicatedInclusiveBP::GshareReplicatedInclusiveBPStats::GshareReplicatedInclusiveBPStats(
    statistics::Group *parent)
    : statistics::Group(parent),
    ADD_STAT(lookupUsedGhrExclusive, statistics::units::Count::get(),
    "Lookups done on the exclusive global history"),
    ADD_STAT(lookupUsedGhrInclusive, statistics::units::Count::get(),
    "Lookups done on the inclusive global history (the one that is always updated)"),
    ADD_STAT(updateUsedGhrExclusive, statistics::units::Count::get(),
    "Updates on the exclusive global history"),
    ADD_STAT(updateUsedGhrInclusive, statistics::units::Count::get(),
    "Updates on the inclusive global history (the one that is always updated)")
{
}

uint32_t
GshareReplicatedInclusiveBP::getIcacheSet(Addr addr) const
{
    return (addr >> icacheBlockShift);
}

} // namespace branch_prediction
} // namespace gem5
