#include <chrono>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <future>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

#include "llvm/Support/CommandLine.h"
#include "mcpsi/context/register.h"
#include "mcpsi/cr/cr.h"
#include "mcpsi/cr/true_cr.h"
#include "mcpsi/utils/vec_op.h"
#include "yacl/link/link.h"

namespace {

using mcpsi::BdozTy;
using mcpsi::Context;
namespace internal = mcpsi::internal;

llvm::cl::opt<std::string> cl_sender_addr(
    "sender_addr", llvm::cl::init("127.0.0.1"),
    llvm::cl::desc("address of sender party (rank 0)"));
llvm::cl::opt<uint32_t> cl_sender_port(
    "sender_port", llvm::cl::init(39530),
    llvm::cl::desc("port of sender party (rank 0)"));
llvm::cl::opt<std::string> cl_receiver_addr(
    "receiver_addr", llvm::cl::init("127.0.0.1"),
    llvm::cl::desc("address of receiver party (rank 1)"));
llvm::cl::opt<uint32_t> cl_receiver_port(
    "receiver_port", llvm::cl::init(39531),
    llvm::cl::desc("port of receiver party (rank 1)"));
llvm::cl::opt<uint32_t> cl_rank("rank", llvm::cl::init(0),
                                llvm::cl::desc("self rank: 0 or 1"));
llvm::cl::opt<uint32_t> cl_chunk("chunk", llvm::cl::init(1024),
                                 llvm::cl::desc("triples generated per chunk"));
llvm::cl::opt<uint32_t> cl_nums("nums", llvm::cl::init(10000),
                                llvm::cl::desc("total triples to generate"));
llvm::cl::opt<std::string> cl_triples_out(
    "triples_out", llvm::cl::Required,
    llvm::cl::desc("path to write generated BDOZ triples"));
llvm::cl::opt<std::string> cl_key_out(
    "key_out", llvm::cl::Required,
    llvm::cl::desc("path to write sampled BDOZ key"));

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
  auto cr = std::dynamic_pointer_cast<mcpsi::TrueCorrelation>(
      ctx->GetState<mcpsi::Correlation>());
  YACL_ENFORCE(cr != nullptr);
  return {cr->BdozTriple(num), cr->GetKey()};
}

void AppendBdozTriples(BdozTy& dst, const BdozTy& src) {
  dst.a.insert(dst.a.end(), src.a.begin(), src.a.end());
  dst.a_pad.insert(dst.a_pad.end(), src.a_pad.begin(), src.a_pad.end());
  dst.peer_a_mac.insert(dst.peer_a_mac.end(), src.peer_a_mac.begin(),
                        src.peer_a_mac.end());

  dst.b.insert(dst.b.end(), src.b.begin(), src.b.end());
  dst.b_pad.insert(dst.b_pad.end(), src.b_pad.begin(), src.b_pad.end());
  dst.peer_b_mac.insert(dst.peer_b_mac.end(), src.peer_b_mac.begin(),
                        src.peer_b_mac.end());

  dst.c.insert(dst.c.end(), src.c.begin(), src.c.end());
  dst.c_pad.insert(dst.c_pad.end(), src.c_pad.begin(), src.c_pad.end());
  dst.peer_c_mac.insert(dst.peer_c_mac.end(), src.peer_c_mac.begin(),
                        src.peer_c_mac.end());
}

std::vector<internal::PTy> ExchangePTyVector(
    const std::shared_ptr<Context>& ctx, const std::vector<internal::PTy>& in) {
  auto conn = ctx->GetConnection();
  auto recv = conn->Exchange(
      yacl::ByteContainerView(in.data(), in.size() * sizeof(internal::PTy)));
  YACL_ENFORCE(static_cast<uint64_t>(recv.size()) ==
               in.size() * sizeof(internal::PTy));
  std::vector<internal::PTy> out(in.size());
  memcpy(out.data(), recv.data(), recv.size());
  return out;
}

internal::PTy ExchangePTy(const std::shared_ptr<Context>& ctx,
                          const internal::PTy& in) {
  auto conn = ctx->GetConnection();
  auto recv = conn->Exchange(yacl::ByteContainerView(&in, sizeof(in)));
  YACL_ENFORCE(recv.size() == sizeof(in));
  internal::PTy out = 0;
  memcpy(&out, recv.data(), sizeof(in));
  return out;
}

BdozTy ExchangeBdozTy(const std::shared_ptr<Context>& ctx, const BdozTy& local) {
  BdozTy remote;
  remote.a = ExchangePTyVector(ctx, local.a);
  remote.a_pad = ExchangePTyVector(ctx, local.a_pad);
  remote.peer_a_mac = ExchangePTyVector(ctx, local.peer_a_mac);
  remote.b = ExchangePTyVector(ctx, local.b);
  remote.b_pad = ExchangePTyVector(ctx, local.b_pad);
  remote.peer_b_mac = ExchangePTyVector(ctx, local.peer_b_mac);
  remote.c = ExchangePTyVector(ctx, local.c);
  remote.c_pad = ExchangePTyVector(ctx, local.c_pad);
  remote.peer_c_mac = ExchangePTyVector(ctx, local.peer_c_mac);
  return remote;
}

void DumpTriples(const std::string& path, size_t rank, const BdozTy& triples) {
  std::ofstream ofs(path, std::ios::out | std::ios::trunc);
  YACL_ENFORCE(ofs.good(), "failed to open triples output file: {}", path);

  ofs << "rank=" << rank << '\n';
  ofs << "count=" << triples.a.size() << '\n';
  ofs << "index,a,b,c,a_pad,b_pad,c_pad,peer_a_mac,peer_b_mac,peer_c_mac\n";
  for (size_t i = 0; i < triples.a.size(); ++i) {
    ofs << i << "," << triples.a[i].GetVal() << "," << triples.b[i].GetVal()
        << "," << triples.c[i].GetVal() << "," << triples.a_pad[i].GetVal()
        << "," << triples.b_pad[i].GetVal() << "," << triples.c_pad[i].GetVal()
        << "," << triples.peer_a_mac[i].GetVal() << ","
        << triples.peer_b_mac[i].GetVal() << ","
        << triples.peer_c_mac[i].GetVal() << '\n';
  }
}

void DumpKey(const std::string& path, size_t rank, const internal::PTy& key) {
  std::ofstream ofs(path, std::ios::out | std::ios::trunc);
  YACL_ENFORCE(ofs.good(), "failed to open key output file: {}", path);
  ofs << "rank=" << rank << '\n';
  ofs << "bdoz_key=" << key.GetVal() << '\n';
}

std::shared_ptr<yacl::link::Context> MakeTcpLink(const std::string& sender_addr,
                                                  uint32_t sender_port,
                                                  const std::string& receiver_addr,
                                                  uint32_t receiver_port,
                                                  size_t rank) {
  yacl::link::ContextDesc desc;
  desc.parties.emplace_back("party0",
                            sender_addr + ":" + std::to_string(sender_port));
  desc.parties.emplace_back("party1",
                            receiver_addr + ":" + std::to_string(receiver_port));
  desc.throttle_window_size = 0;
  desc.http_timeout_ms = 120 * 1000;
  auto lctx = yacl::link::FactoryBrpc().CreateContext(desc, rank);
  lctx->ConnectToMesh();
  return lctx;
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

int main(int argc, char** argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv);
  YACL_ENFORCE(cl_rank.getValue() <= 1, "rank must be 0 or 1");
  YACL_ENFORCE(cl_chunk.getValue() > 0, "chunk must be positive");
  YACL_ENFORCE(cl_nums.getValue() > 0, "nums must be positive");

  auto lctx = MakeTcpLink(cl_sender_addr.getValue(), cl_sender_port,
                          cl_receiver_addr.getValue(), cl_receiver_port,
                          cl_rank.getValue());
  auto ctx = std::make_shared<mcpsi::Context>(lctx);

  auto setup_begin = Snapshot(ctx);
  auto setup_time_begin = std::chrono::high_resolution_clock::now();
  mcpsi::SetupContext(ctx, true);
  auto setup_time_end = std::chrono::high_resolution_clock::now();
  auto setup_end = Snapshot(ctx);

  auto triple_begin = Snapshot(ctx);
  auto triple_time_begin = std::chrono::high_resolution_clock::now();

  BdozTy local_triples;
  internal::PTy local_key = 0;
  size_t generated = 0;
  const size_t total = cl_nums.getValue();
  const size_t chunk = cl_chunk.getValue();

  while (generated < total) {
    const size_t cur = std::min(chunk, total - generated);
    auto [chunk_triples, chunk_key] = GenerateBdozTriples(ctx, cur);
    if (generated == 0) {
      local_key = chunk_key;
    } else {
      YACL_ENFORCE(local_key == chunk_key, "inconsistent key across chunks");
    }
    AppendBdozTriples(local_triples, chunk_triples);
    generated += cur;
  }

  auto triple_time_end = std::chrono::high_resolution_clock::now();
  auto triple_end = Snapshot(ctx);

  auto setup_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      setup_time_end - setup_time_begin)
                      .count();
  auto triple_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                       triple_time_end - triple_time_begin)
                       .count();

  std::cout << "rank=" << cl_rank.getValue() << '\n';
  std::cout << "triples=" << total << '\n';
  std::cout << "chunk=" << chunk << '\n';
  std::cout << "setup_ms=" << setup_ms << '\n';
  PrintStats("setup", cl_rank.getValue(), Diff(setup_end, setup_begin));
  std::cout << "bdoz_triple_generation_ms=" << triple_ms << '\n';
  PrintStats("bdoz_triple_generation", cl_rank.getValue(),
             Diff(triple_end, triple_begin));
  std::cout << "note=TrueCorrelation::BdozTriple already runs internal "
               "sacrifice verification (CheckBdozTriple)\n";

  DumpTriples(cl_triples_out.getValue(), cl_rank.getValue(), local_triples);
  DumpKey(cl_key_out.getValue(), cl_rank.getValue(), local_key);

  auto verify_begin = std::chrono::high_resolution_clock::now();
  auto remote_triples = ExchangeBdozTy(ctx, local_triples);
  auto remote_key = ExchangePTy(ctx, local_key);

  if (cl_rank.getValue() == 0) {
    VerifyBdozTriples(local_triples, local_key, remote_triples, remote_key);
  } else {
    VerifyBdozTriples(remote_triples, remote_key, local_triples, local_key);
  }
  auto verify_end = std::chrono::high_resolution_clock::now();
  auto verify_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                       verify_end - verify_begin)
                       .count();
  std::cout << "verify_ms=" << verify_ms << '\n';
  std::cout << "verified=true\n";

  return 0;
}
