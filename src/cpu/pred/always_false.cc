#include "cpu/pred/always_false.hh"

#include "base/intmath.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/Fetch.hh"

namespace gem5
{

namespace branch_prediction
{

AlwaysFalseBP::AlwaysFalseBP(const AlwaysFalseBPParams &params)
    : ConditionalPredictor(params)
{}

void AlwaysFalseBP::branchPlaceholder(ThreadID tid, Addr pc,
                                bool uncond, void * &bpHistory)
{}

void
AlwaysFalseBP::updateHistories(ThreadID tid, Addr pc, bool uncond, bool taken,
                         Addr target, const StaticInstPtr &inst,
                         void * &bp_history)
{}


bool
AlwaysFalseBP::lookup(ThreadID tid, Addr branch_addr, void * &bp_history)
{
    DPRINTF(Fetch, "prediction is False, as always.\n");

    return false;
}

void
AlwaysFalseBP::update(ThreadID tid, Addr branch_addr, bool taken, void *&bp_history,
                bool squashed, const StaticInstPtr & inst, Addr target)
{}


} // namespace branch_prediction
} // namespace gem5
