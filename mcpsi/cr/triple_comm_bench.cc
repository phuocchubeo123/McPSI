#include <chrono>
#include <future>
#include <iostream>
#include <tuple>
#include <vector>

#include "mcpsi/context/register.h"
#include "mcpsi/cr/cr.h"
#include "mcpsi/cr/true_cr.h"
#include "mcpsi/utils/test_util.h"
#include "mcpsi/utils/vec_op.h"

namespace mcpsi {
namespace {

struct CommStats {
  uint64_t sent_bytes;
  uint64_t recv_bytes;
  uint64_t sent_actions;
  uint64_t recv_actions;
};

CommStats Snapshot(const std::shared_ptr<Context>& ctx) {
  auto stats = ctx->GetConnection()->GetStats();
  return {stats->sent_bytes, stats->recv_bytes, stats->sent_actions,
          stats->recv_actions};
}

CommStats Diff(const CommStats& end, const CommStats& begin) {
  return {end.sent_bytes - begin.sent_bytes, end.recv_bytes - begin.recv_bytes,
          end.sent_actions - begin.sent_actions,
          end.recv_actions - begin.recv_actions};
}

void PrintStats(const char* phase, size_t rank, const CommStats& stats) {
  std::cout << phase << " rank=" << rank << " sent_bytes=" << stats.sent_bytes
            << " recv_bytes=" << stats.recv_bytes
            << " sent_actions=" << stats.sent_actions
            << " recv_actions=" << stats.recv_actions << '\n';
}

std::tuple<BdozTy, internal::PTy> GenerateBdozTriples(
    const std::shared_ptr<Context>& ctx, size_t num) {
  auto cr = std::dynamic_pointer_cast<TrueCorrelation>(
      ctx->GetState<Correlation>());
  YACL_ENFORCE(cr != nullptr);
  return {cr->BdozTriple(num), cr->GetKey()};
}

void VerifyBdozTriples(const BdozTy& rank0, internal::PTy delta0,
                       const BdozTy& rank1, internal::PTy delta1) {
  YACL_ENFORCE(rank0.a.size() == rank1.a.size());
  for (size_t i = 0; i < rank0.a.size(); ++i) {
    auto a = rank0.a[i] + rank1.a[i];
    auto b = rank0.b[i] + rank1.b[i];
    YACL_ENFORCE(a * b == rank0.c[i] + rank1.c[i],
                 "bad BDOZ triple at index {}", i);
    YACL_ENFORCE(rank0.peer_a_mac[i] ==
                     rank1.a[i] * delta0 + rank1.a_pad[i],
                 "bad rank 0 peer a MAC at index {}", i);
    YACL_ENFORCE(rank0.peer_b_mac[i] ==
                     rank1.b[i] * delta0 + rank1.b_pad[i],
                 "bad rank 0 peer b MAC at index {}", i);
    YACL_ENFORCE(rank0.peer_c_mac[i] == rank1.c[i] * delta0 + rank1.c_pad[i],
                 "bad rank 0 peer c MAC at index {}", i);
    YACL_ENFORCE(rank1.peer_a_mac[i] ==
                     rank0.a[i] * delta1 + rank0.a_pad[i],
                 "bad rank 1 peer a MAC at index {}", i);
    YACL_ENFORCE(rank1.peer_b_mac[i] ==
                     rank0.b[i] * delta1 + rank0.b_pad[i],
                 "bad rank 1 peer b MAC at index {}", i);
    YACL_ENFORCE(rank1.peer_c_mac[i] == rank0.c[i] * delta1 + rank0.c_pad[i],
                 "bad rank 1 peer c MAC at index {}", i);
  }
}

}  // namespace
}  // namespace mcpsi

int main() {
  constexpr size_t kTripleNum = 1000;

  auto contexts = mcpsi::MockContext(2);

  auto setup_begin0 = mcpsi::Snapshot(contexts[0]);
  auto setup_begin1 = mcpsi::Snapshot(contexts[1]);
  auto setup_time_begin = std::chrono::high_resolution_clock::now();

  auto setup0 = std::async([&] { mcpsi::SetupContext(contexts[0], true); });
  auto setup1 = std::async([&] { mcpsi::SetupContext(contexts[1], true); });
  setup0.get();
  setup1.get();

  auto setup_time_end = std::chrono::high_resolution_clock::now();
  auto setup_end0 = mcpsi::Snapshot(contexts[0]);
  auto setup_end1 = mcpsi::Snapshot(contexts[1]);

  auto triple_begin0 = mcpsi::Snapshot(contexts[0]);
  auto triple_begin1 = mcpsi::Snapshot(contexts[1]);
  auto triple_time_begin = std::chrono::high_resolution_clock::now();

  auto triples0 = std::async(
      [&] { return mcpsi::GenerateBdozTriples(contexts[0], kTripleNum); });
  auto triples1 = std::async(
      [&] { return mcpsi::GenerateBdozTriples(contexts[1], kTripleNum); });

  auto [rank0_triples, delta0] = triples0.get();
  auto [rank1_triples, delta1] = triples1.get();

  auto triple_time_end = std::chrono::high_resolution_clock::now();
  auto triple_end0 = mcpsi::Snapshot(contexts[0]);
  auto triple_end1 = mcpsi::Snapshot(contexts[1]);

  mcpsi::VerifyBdozTriples(rank0_triples, delta0, rank1_triples, delta1);

  auto setup_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      setup_time_end - setup_time_begin)
                      .count();
  auto triple_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                       triple_time_end - triple_time_begin)
                       .count();

  std::cout << "triples=" << kTripleNum << '\n';
  std::cout << "setup_ms=" << setup_ms << '\n';
  mcpsi::PrintStats("setup", 0, mcpsi::Diff(setup_end0, setup_begin0));
  mcpsi::PrintStats("setup", 1, mcpsi::Diff(setup_end1, setup_begin1));
  std::cout << "bdoz_triple_generation_ms=" << triple_ms << '\n';
  mcpsi::PrintStats("bdoz_triple_generation", 0,
                    mcpsi::Diff(triple_end0, triple_begin0));
  mcpsi::PrintStats("bdoz_triple_generation", 1,
                    mcpsi::Diff(triple_end1, triple_begin1));
  std::cout << "verified=true\n";

  return 0;
}
