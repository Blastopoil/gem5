#include "cpu/pred/random_predictor.hh"

#include "base/intmath.hh"
#include "base/logging.hh"
#include "base/random.hh"
#include "base/trace.hh"
#include "debug/Fetch.hh"

namespace gem5
{

namespace branch_prediction
{

RandomBP::RandomBP(const RandomBPParams &params)
    : ConditionalPredictor(params),
      rng(Random::genRandom())
{}

void RandomBP::branchPlaceholder(ThreadID tid, Addr pc,
                                bool uncond, void * &bpHistory)
{}

void
RandomBP::updateHistories(ThreadID tid, Addr pc, bool uncond, bool taken,
                         Addr target, const StaticInstPtr &inst,
                         void * &bp_history)
{}


bool
RandomBP::lookup(ThreadID tid, Addr branch_addr, void * &bp_history)
{
    bool pred = (rng->random<uint8_t>() & 1) != 0;
    DPRINTF(Fetch, "Random prediction\n");
    return pred;
}

void
RandomBP::update(ThreadID tid, Addr branch_addr, bool taken, void *&bp_history,
                bool squashed, const StaticInstPtr & inst, Addr target)
{}


} // namespace branch_prediction
} // namespace gem5
