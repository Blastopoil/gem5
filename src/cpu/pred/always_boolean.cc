#include "cpu/pred/always_boolean.hh"

#include "base/intmath.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/AlwaysBooleanBP.hh"

namespace gem5
{

namespace branch_prediction
{

AlwaysBooleanBP::AlwaysBooleanBP(const AlwaysBooleanBPParams &params)
    : ConditionalPredictor(params),
      alwaysTruePreds(params.alwaysTruePreds)
{}

void AlwaysBooleanBP::branchPlaceholder(ThreadID tid, Addr pc,
                                bool uncond, void * &bpHistory)
{}

void
AlwaysBooleanBP::updateHistories(ThreadID tid, Addr pc, bool uncond, bool taken,
                         Addr target, const StaticInstPtr &inst,
                         void * &bp_history)
{}


bool
AlwaysBooleanBP::lookup(ThreadID tid, Addr branch_addr, void * &bp_history)
{
    if (alwaysTruePreds) {
        DPRINTF(AlwaysBooleanBP, "prediction is true, as always.\n");
    } else {
        DPRINTF(AlwaysBooleanBP, "prediction is false, as always.\n");
    }

    return alwaysTruePreds;
}

void
AlwaysBooleanBP::update(ThreadID tid, Addr branch_addr, bool taken, void *&bp_history,
                bool squashed, const StaticInstPtr & inst, Addr target)
{}


} // namespace branch_prediction
} // namespace gem5
