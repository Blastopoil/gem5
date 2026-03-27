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

#include "cpu/pred/gshare_replicated_bhr.hh"

#include "base/bitfield.hh"
#include "base/intmath.hh"
#include "mem/cache/base.hh"

namespace gem5
{

namespace branch_prediction
{

GshareReplicatedBP::GshareReplicatedBP(const GshareReplicatedBPParams &params)
    : ConditionalPredictor(params),
      globalHistoryReg(params.numThreads, 0),
      globalHistoryReg2(params.numThreads, 0),
      globalHistoryBits(ceilLog2(params.global_predictor_size)),
      globalPredictorSize(params.global_predictor_size),
      globalCtrBits(params.global_counter_bits),
      globalCtrs(globalPredictorSize, SatCounter8(globalCtrBits)),
      instructionShift(params.instruction_shift),
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
GshareReplicatedBP::uncondBranch(ThreadID tid, Addr pc, void *&bp_history)
{
    uint32_t instruction_dir = getInstructionDir(pc);
    bool oddDir = (instruction_dir & 1U) != 0;

    BPHistory *history = new BPHistory;
    history->globalHistoryReg = oddDir ? globalHistoryReg2[tid]
                                       : globalHistoryReg[tid];
    history->finalPred = true;
    history->oddDir = oddDir;
    bp_history = static_cast<void *>(history);
}

void
GshareReplicatedBP::updateHistories(ThreadID tid, Addr pc, bool uncond, bool taken,
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
GshareReplicatedBP::branchPlaceholder(ThreadID tid, Addr pc,
                                bool uncond, void * &bpHistory)
{
// Placeholder for a function that only returns history items
}

void
GshareReplicatedBP::squash(ThreadID tid, void *&bp_history)
{
    BPHistory *history = static_cast<BPHistory *>(bp_history);
    if (history->oddDir)
        globalHistoryReg2[tid] = history->globalHistoryReg;
    else
        globalHistoryReg[tid] = history->globalHistoryReg;

    delete history;
    bp_history = nullptr;
}

/*
 * Here we lookup the actual branch prediction. A hash of
 * the global history register and a branch's PC is used to
 * index into the counter, which both present a prediction.
 */
bool
GshareReplicatedBP::lookup(ThreadID tid, Addr branchAddr, void *&bp_history)
{
    uint32_t instruction_dir = getInstructionDir(branchAddr);
    bool oddDir = (instruction_dir & 1U) != 0;

    unsigned selectedGhr;
    if (oddDir) {
        selectedGhr = globalHistoryReg2[tid];
        stats.lookupUsedGhr2++;
    } else {
        selectedGhr = globalHistoryReg[tid];
        stats.lookupUsedGhr1++;
    }

    unsigned globalHistoryIdx =
        (((branchAddr >> instShiftAmt) ^ selectedGhr) &
         globalHistoryMask);

    assert(globalHistoryIdx < globalPredictorSize);

    bool final_prediction = globalCtrs[globalHistoryIdx] > takenThreshold;

    BPHistory *history = new BPHistory;
    history->globalHistoryReg = selectedGhr;
    history->finalPred = final_prediction;
    bp_history = static_cast<void *>(history);

    return final_prediction;
}

/* Updates the counter values based on the actual branch
 * direction.
 */
void
GshareReplicatedBP::update(ThreadID tid, Addr branchAddr, bool taken,
                 void *&bp_history, bool squashed,
                 const StaticInstPtr &inst, Addr target)
{
    uint32_t instruction_dir = getInstructionDir(branchAddr);
    bool oddDir = (instruction_dir & 1U) != 0;

    assert(bp_history);

    BPHistory *history = static_cast<BPHistory *>(bp_history);

    // We do not update the counters speculatively on a squash.
    // We just restore the global history register.
    if (squashed) {
        if (oddDir)
            globalHistoryReg2[tid] = (history->globalHistoryReg << 1) | taken;
        else
            globalHistoryReg[tid] = (history->globalHistoryReg << 1) | taken;
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
GshareReplicatedBP::updateGlobalHistReg(ThreadID tid, Addr branchAddr, bool taken)
{
    uint32_t instruction_dir = getInstructionDir(branchAddr);
    bool oddDir = (instruction_dir & 1U) != 0;

    if (oddDir) {
        unsigned selectedGhr = taken ? (globalHistoryReg2[tid] << 1) | 1
                                     : (globalHistoryReg2[tid] << 1);
        selectedGhr &= historyRegisterMask;
        globalHistoryReg2[tid] = selectedGhr;
        stats.updateUsedGhr2++;
    }
    else {
        unsigned selectedGhr = taken ? (globalHistoryReg[tid] << 1) | 1
                                     : (globalHistoryReg[tid] << 1);
        selectedGhr &= historyRegisterMask;
        globalHistoryReg[tid] = selectedGhr;
        stats.updateUsedGhr1++;
    }
}

GshareReplicatedBP::GshareReplicatedBPStats::GshareReplicatedBPStats(
    statistics::Group *parent)
    : statistics::Group(parent),
    ADD_STAT(lookupUsedGhr1, statistics::units::Count::get(),
    "Lookups done on globalHistoryReg"),
    ADD_STAT(lookupUsedGhr2, statistics::units::Count::get(),
    "Lookups done on globalHistoryReg2"),
    ADD_STAT(updateUsedGhr1, statistics::units::Count::get(),
    "Updates on globalHistoryReg"),
    ADD_STAT(updateUsedGhr2, statistics::units::Count::get(),
    "Updates on globalHistoryReg2")
{
}

uint32_t
GshareReplicatedBP::getInstructionDir(Addr addr) const
{
    return (addr >> instructionShift);
}

} // namespace branch_prediction
} // namespace gem5
