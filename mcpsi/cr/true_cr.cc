#include "mcpsi/cr/true_cr.h"

#include "mcpsi/cr/utils/ot_helper.h"
#include "mcpsi/ss/type.h"
#include "mcpsi/utils/vec_op.h"

namespace mcpsi {

void TrueCorrelation::BeaverTriple(absl::Span<internal::ATy> a,
                                   absl::Span<internal::ATy> b,
                                   absl::Span<internal::ATy> c) {
  const size_t num = c.size();
  YACL_ENFORCE(num == a.size());
  YACL_ENFORCE(num == b.size());

  auto p_abcAC = internal::op::Zeros(num * 5);
  auto p_abcAC_span = absl::MakeSpan(p_abcAC);
  auto p_a = p_abcAC_span.subspan(0 * num, num);
  auto p_b = p_abcAC_span.subspan(1 * num, num);
  auto p_c = p_abcAC_span.subspan(2 * num, num);
  auto p_A = p_abcAC_span.subspan(3 * num, num);
  auto p_C = p_abcAC_span.subspan(4 * num, num);

  internal::op::Rand(absl::MakeSpan(p_b));

  auto conn = ctx_->GetConnection();
  ot::OtHelper(ot_sender_, ot_receiver_)
      .BeaverTripleExtendWithChosenB(conn, p_a, p_b, p_c, p_A, p_C);

  std::vector<internal::ATy> auth_abcAC(num * 5);
  auto auth_abcAC_span = absl::MakeSpan(auth_abcAC);

  std::vector<internal::ATy> remote_auth_abcAC(num * 5);
  auto remote_auth_abcAC_span = absl::MakeSpan(remote_auth_abcAC);

  if (ctx_->GetRank() == 0) {
    AuthSet(p_abcAC_span, auth_abcAC_span);
    AuthGet(remote_auth_abcAC_span);
  } else {
    AuthGet(remote_auth_abcAC_span);
    AuthSet(p_abcAC_span, auth_abcAC_span);
  }

  // length double
  internal::op::Add(
      absl::MakeConstSpan(
          reinterpret_cast<const internal::PTy*>(auth_abcAC.data()), 10 * num),
      absl::MakeConstSpan(
          reinterpret_cast<const internal::PTy*>(remote_auth_abcAC.data()),
          10 * num),
      absl::MakeSpan(reinterpret_cast<internal::PTy*>(auth_abcAC.data()),
                     10 * num));

  // return value
  auto auth_a = auth_abcAC_span.subspan(0 * num, num);
  auto auth_b = auth_abcAC_span.subspan(1 * num, num);
  auto auth_c = auth_abcAC_span.subspan(2 * num, num);
  memcpy(a.data(), auth_a.data(), num * sizeof(internal::ATy));
  memcpy(b.data(), auth_b.data(), num * sizeof(internal::ATy));
  memcpy(c.data(), auth_c.data(), num * sizeof(internal::ATy));

  // ---- consistency check ----
  auto auth_A = auth_abcAC_span.subspan(3 * num, num);
  auto auth_C = auth_abcAC_span.subspan(4 * num, num);
  auto seed = conn->SyncSeed();
  auto p_coef = internal::op::Rand(seed, num);
  std::vector<internal::ATy> coef(num, {0, 0});
  std::transform(p_coef.cbegin(), p_coef.cend(), coef.begin(),
                 [](const internal::PTy& val) -> internal::ATy {
                   return {val, val};
                 });

  internal::op::Mul(
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(auth_A.data()),
                          2 * num),
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(coef.data()),
                          2 * num),
      absl::MakeSpan(reinterpret_cast<internal::PTy*>(auth_A.data()), 2 * num));

  internal::op::Add(
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(auth_a.data()),
                          2 * num),
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(auth_A.data()),
                          2 * num),
      absl::MakeSpan(reinterpret_cast<internal::PTy*>(auth_A.data()), 2 * num));

  internal::op::Mul(
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(auth_C.data()),
                          2 * num),
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(coef.data()),
                          2 * num),
      absl::MakeSpan(reinterpret_cast<internal::PTy*>(auth_C.data()), 2 * num));

  internal::op::Add(
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(auth_c.data()),
                          2 * num),
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(auth_C.data()),
                          2 * num),
      absl::MakeSpan(reinterpret_cast<internal::PTy*>(auth_C.data()), 2 * num));

  // internal::PTy type
  auto aA_cC = OpenAndCheck(auth_abcAC_span.subspan(3 * num, 2 * num));
  auto aA_cC_span = absl::MakeSpan(aA_cC);
  auto aA = aA_cC_span.subspan(0, num);
  auto cC = aA_cC_span.subspan(num, num);

  internal::op::Mul(aA, p_b, aA);

  auto buf = conn->Exchange(
      yacl::ByteContainerView(aA.data(), num * sizeof(internal::PTy)));
  YACL_ENFORCE(static_cast<uint64_t>(buf.size()) ==
               num * sizeof(internal::PTy));
  auto remote_aA =
      absl::MakeSpan(reinterpret_cast<internal::PTy*>(buf.data()), num);

  internal::op::Add(aA, remote_aA, aA);
  for (size_t i = 0; i < num; ++i) {
    YACL_ENFORCE(cC[i] == aA[i], "{} : cC is {}", i, cC[i].GetVal());
  }
  // ---- consistency check ----
}

// TODO: Current is the same as BeaverTriple
void TrueCorrelation::DyBeaverTripleGet(absl::Span<internal::ATy> a,
                                        absl::Span<internal::ATy> b,
                                        absl::Span<internal::ATy> c,
                                        absl::Span<internal::ATy> r) {
  const size_t num = c.size();
  YACL_ENFORCE(num == a.size());
  YACL_ENFORCE(num == b.size());

  auto p_abcAC = internal::op::Zeros(num * 5);
  auto p_abcAC_span = absl::MakeSpan(p_abcAC);
  auto p_a = p_abcAC_span.subspan(0 * num, num);
  auto p_b = p_abcAC_span.subspan(1 * num, num);
  auto p_c = p_abcAC_span.subspan(2 * num, num);
  auto p_A = p_abcAC_span.subspan(3 * num, num);
  auto p_C = p_abcAC_span.subspan(4 * num, num);

  auto conn = ctx_->GetConnection();
  ot::OtHelper(ot_sender_, ot_receiver_)
      .MulPPExtendRecvWithChosenB(conn, p_a, p_c, p_A, p_C);

  std::vector<internal::ATy> auth_abcAC(num * 5);
  auto auth_abcAC_span = absl::MakeSpan(auth_abcAC);

  std::vector<internal::ATy> remote_auth_abcAC(num * 5);
  auto remote_auth_abcAC_span = absl::MakeSpan(remote_auth_abcAC);

  if (ctx_->GetRank() == 0) {
    AuthSet(p_abcAC_span, auth_abcAC_span);
    AuthGet(remote_auth_abcAC_span);
  } else {
    AuthGet(remote_auth_abcAC_span);
    AuthSet(p_abcAC_span, auth_abcAC_span);
  }

  // length double
  internal::op::Add(
      absl::MakeConstSpan(
          reinterpret_cast<const internal::PTy*>(auth_abcAC.data()), 10 * num),
      absl::MakeConstSpan(
          reinterpret_cast<const internal::PTy*>(remote_auth_abcAC.data()),
          10 * num),
      absl::MakeSpan(reinterpret_cast<internal::PTy*>(auth_abcAC.data()),
                     10 * num));

  // return value
  auto auth_a = auth_abcAC_span.subspan(0 * num, num);
  auto auth_b = auth_abcAC_span.subspan(1 * num, num);
  auto auth_c = auth_abcAC_span.subspan(2 * num, num);
  memcpy(a.data(), auth_a.data(), num * sizeof(internal::ATy));
  memcpy(b.data(), auth_b.data(), num * sizeof(internal::ATy));
  memcpy(c.data(), auth_c.data(), num * sizeof(internal::ATy));

  // ---- consistency check ----
  auto auth_A = auth_abcAC_span.subspan(3 * num, num);
  auto auth_C = auth_abcAC_span.subspan(4 * num, num);
  auto seed = conn->SyncSeed();
  auto p_coef = internal::op::Rand(seed, num);
  std::vector<internal::ATy> coef(num, {0, 0});
  std::transform(p_coef.cbegin(), p_coef.cend(), coef.begin(),
                 [](const internal::PTy& val) -> internal::ATy {
                   return {val, val};
                 });

  internal::op::Mul(
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(auth_A.data()),
                          2 * num),
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(coef.data()),
                          2 * num),
      absl::MakeSpan(reinterpret_cast<internal::PTy*>(auth_A.data()), 2 * num));

  internal::op::Add(
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(auth_a.data()),
                          2 * num),
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(auth_A.data()),
                          2 * num),
      absl::MakeSpan(reinterpret_cast<internal::PTy*>(auth_A.data()), 2 * num));

  internal::op::Mul(
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(auth_C.data()),
                          2 * num),
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(coef.data()),
                          2 * num),
      absl::MakeSpan(reinterpret_cast<internal::PTy*>(auth_C.data()), 2 * num));

  internal::op::Add(
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(auth_c.data()),
                          2 * num),
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(auth_C.data()),
                          2 * num),
      absl::MakeSpan(reinterpret_cast<internal::PTy*>(auth_C.data()), 2 * num));

  // internal::PTy type
  auto aA_cC = OpenAndCheck(auth_abcAC_span.subspan(3 * num, 2 * num));
  auto aA_cC_span = absl::MakeSpan(aA_cC);
  auto aA = aA_cC_span.subspan(0, num);
  auto cC = aA_cC_span.subspan(num, num);

  internal::op::Mul(aA, p_b, aA);

  auto buf = conn->Exchange(
      yacl::ByteContainerView(aA.data(), num * sizeof(internal::PTy)));
  YACL_ENFORCE(static_cast<uint64_t>(buf.size()) ==
               num * sizeof(internal::PTy));
  auto remote_aA =
      absl::MakeSpan(reinterpret_cast<internal::PTy*>(buf.data()), num);

  internal::op::Add(aA, remote_aA, aA);
  for (size_t i = 0; i < num; ++i) {
    YACL_ENFORCE(cC[i] == aA[i], "{} : cC is {}", i, cC[i].GetVal());
  }
  // ---- consistency check ----
  // Dy
  auto delta = dy_key_.val;
  std::vector<internal::PTy> R(num * 2, 0);
  std::vector<internal::PTy> A(num * 2, 0);
  std::vector<internal::PTy> B(num * 2, 0);

  dy_key_sender_->rsend(absl::MakeSpan(R));
  // ot::OtHelper(ot_sender_, ot_receiver_)
  //     .BaseVoleSend(conn, delta, absl::MakeSpan(R));
  dy_key_receiver_->rrecv(absl::MakeSpan(A), absl::MakeSpan(B));
  // ot::OtHelper(ot_sender_, ot_receiver_)
  //     .BaseVoleRecv(conn, absl::MakeSpan(A), absl::MakeSpan(B));

  internal::op::SubInplace(absl::MakeSpan(R), absl::MakeSpan(B));
  internal::op::AddInplace(
      absl::MakeSpan(R),
      internal::op::ScalarMul(
          delta,
          absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(a.data()),
                              2 * num)));

  auto diff_A = internal::op::Sub(
      absl::MakeConstSpan(A),
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(a.data()),
                          2 * num));

  auto diff_buf = conn->Exchange(
      yacl::ByteContainerView(diff_A.data(), 2 * num * sizeof(internal::PTy)));
  YACL_ENFORCE(static_cast<uint64_t>(diff_buf.size()) ==
               2 * num * sizeof(internal::PTy));
  auto remote_diff_A = absl::MakeSpan(
      reinterpret_cast<internal::PTy*>(diff_buf.data()), 2 * num);

  internal::op::SubInplace(absl::MakeSpan(R),
                           internal::op::ScalarMul(delta, remote_diff_A));

  memcpy(reinterpret_cast<internal::PTy*>(r.data()), R.data(),
         2 * num * sizeof(internal::PTy));
}

// TODO: Current is the same as BeaverTriple
void TrueCorrelation::DyBeaverTripleSet(absl::Span<internal::ATy> a,
                                        absl::Span<internal::ATy> b,
                                        absl::Span<internal::ATy> c,
                                        absl::Span<internal::ATy> r) {
  const size_t num = c.size();
  YACL_ENFORCE(num == a.size());
  YACL_ENFORCE(num == b.size());

  auto p_abcAC = internal::op::Zeros(num * 5);
  auto p_abcAC_span = absl::MakeSpan(p_abcAC);
  auto p_a = p_abcAC_span.subspan(0 * num, num);
  auto p_b = p_abcAC_span.subspan(1 * num, num);
  auto p_c = p_abcAC_span.subspan(2 * num, num);
  auto p_A = p_abcAC_span.subspan(3 * num, num);
  auto p_C = p_abcAC_span.subspan(4 * num, num);

  internal::op::Rand(absl::MakeSpan(p_b));

  auto conn = ctx_->GetConnection();
  ot::OtHelper(ot_sender_, ot_receiver_)
      .MulPPExtendSendWithChosenB(conn, p_b, p_c, p_C);

  internal::op::Rand(absl::MakeSpan(p_a));
  internal::op::Rand(absl::MakeSpan(p_A));

  internal::op::AddInplace(p_c, internal::op::Mul(p_a, p_b));
  internal::op::AddInplace(p_C, internal::op::Mul(p_A, p_b));

  // auto conn = ctx_->GetConnection();
  // ot::OtHelper(ot_sender_, ot_receiver_)
  //     .BeaverTripleExtendWithChosenB(conn, p_a, p_b, p_c, p_A, p_C);

  std::vector<internal::ATy> auth_abcAC(num * 5);
  auto auth_abcAC_span = absl::MakeSpan(auth_abcAC);

  std::vector<internal::ATy> remote_auth_abcAC(num * 5);
  auto remote_auth_abcAC_span = absl::MakeSpan(remote_auth_abcAC);

  if (ctx_->GetRank() == 0) {
    AuthSet(p_abcAC_span, auth_abcAC_span);
    AuthGet(remote_auth_abcAC_span);
  } else {
    AuthGet(remote_auth_abcAC_span);
    AuthSet(p_abcAC_span, auth_abcAC_span);
  }

  // length double
  internal::op::Add(
      absl::MakeConstSpan(
          reinterpret_cast<const internal::PTy*>(auth_abcAC.data()), 10 * num),
      absl::MakeConstSpan(
          reinterpret_cast<const internal::PTy*>(remote_auth_abcAC.data()),
          10 * num),
      absl::MakeSpan(reinterpret_cast<internal::PTy*>(auth_abcAC.data()),
                     10 * num));

  // return value
  auto auth_a = auth_abcAC_span.subspan(0 * num, num);
  auto auth_b = auth_abcAC_span.subspan(1 * num, num);
  auto auth_c = auth_abcAC_span.subspan(2 * num, num);
  memcpy(a.data(), auth_a.data(), num * sizeof(internal::ATy));
  memcpy(b.data(), auth_b.data(), num * sizeof(internal::ATy));
  memcpy(c.data(), auth_c.data(), num * sizeof(internal::ATy));

  // ---- consistency check ----
  auto auth_A = auth_abcAC_span.subspan(3 * num, num);
  auto auth_C = auth_abcAC_span.subspan(4 * num, num);
  auto seed = conn->SyncSeed();
  auto p_coef = internal::op::Rand(seed, num);
  std::vector<internal::ATy> coef(num, {0, 0});
  std::transform(p_coef.cbegin(), p_coef.cend(), coef.begin(),
                 [](const internal::PTy& val) -> internal::ATy {
                   return {val, val};
                 });

  internal::op::Mul(
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(auth_A.data()),
                          2 * num),
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(coef.data()),
                          2 * num),
      absl::MakeSpan(reinterpret_cast<internal::PTy*>(auth_A.data()), 2 * num));

  internal::op::Add(
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(auth_a.data()),
                          2 * num),
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(auth_A.data()),
                          2 * num),
      absl::MakeSpan(reinterpret_cast<internal::PTy*>(auth_A.data()), 2 * num));

  internal::op::Mul(
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(auth_C.data()),
                          2 * num),
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(coef.data()),
                          2 * num),
      absl::MakeSpan(reinterpret_cast<internal::PTy*>(auth_C.data()), 2 * num));

  internal::op::Add(
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(auth_c.data()),
                          2 * num),
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(auth_C.data()),
                          2 * num),
      absl::MakeSpan(reinterpret_cast<internal::PTy*>(auth_C.data()), 2 * num));

  // internal::PTy type
  auto aA_cC = OpenAndCheck(auth_abcAC_span.subspan(3 * num, 2 * num));
  auto aA_cC_span = absl::MakeSpan(aA_cC);
  auto aA = aA_cC_span.subspan(0, num);
  auto cC = aA_cC_span.subspan(num, num);

  internal::op::Mul(aA, p_b, aA);

  auto buf = conn->Exchange(
      yacl::ByteContainerView(aA.data(), num * sizeof(internal::PTy)));
  YACL_ENFORCE(static_cast<uint64_t>(buf.size()) ==
               num * sizeof(internal::PTy));
  auto remote_aA =
      absl::MakeSpan(reinterpret_cast<internal::PTy*>(buf.data()), num);

  internal::op::Add(aA, remote_aA, aA);
  for (size_t i = 0; i < num; ++i) {
    YACL_ENFORCE(cC[i] == aA[i], "{} : cC is {}", i, cC[i].GetVal());
  }
  // ---- consistency check ----
  // Dy
  auto delta = dy_key_.val;
  std::vector<internal::PTy> R(num * 2, 0);
  std::vector<internal::PTy> A(num * 2, 0);
  std::vector<internal::PTy> B(num * 2, 0);

  dy_key_receiver_->rrecv(absl::MakeSpan(A), absl::MakeSpan(B));
  // ot::OtHelper(ot_sender_, ot_receiver_)
  //     .BaseVoleRecv(conn, absl::MakeSpan(A), absl::MakeSpan(B));
  dy_key_sender_->rsend(absl::MakeSpan(R));
  // ot::OtHelper(ot_sender_, ot_receiver_)
  //     .BaseVoleSend(conn, delta, absl::MakeSpan(R));

  internal::op::SubInplace(absl::MakeSpan(R), absl::MakeSpan(B));
  internal::op::AddInplace(
      absl::MakeSpan(R),
      internal::op::ScalarMul(
          delta,
          absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(a.data()),
                              2 * num)));

  auto diff_A = internal::op::Sub(
      absl::MakeConstSpan(A),
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(a.data()),
                          2 * num));

  auto diff_buf = conn->Exchange(
      yacl::ByteContainerView(diff_A.data(), 2 * num * sizeof(internal::PTy)));
  YACL_ENFORCE(static_cast<uint64_t>(diff_buf.size()) ==
               2 * num * sizeof(internal::PTy));
  auto remote_diff_A = absl::MakeSpan(
      reinterpret_cast<internal::PTy*>(diff_buf.data()), 2 * num);

  internal::op::SubInplace(absl::MakeSpan(R),
                           internal::op::ScalarMul(delta, remote_diff_A));

  memcpy(reinterpret_cast<internal::PTy*>(r.data()), R.data(),
         2 * num * sizeof(internal::PTy));
}

void TrueCorrelation::RandomSet(absl::Span<internal::ATy> out) {
  const size_t num = out.size();
  std::vector<internal::PTy> a(num);
  std::vector<internal::PTy> b(num);
  // a * remote_key + b = remote_c
  vole_receiver_->rrecv(absl::MakeSpan(a), absl::MakeSpan(b));
  // mac = a * key_
  auto mac = internal::op::ScalarMul(key_, absl::MakeConstSpan(a));
  // a's mac = a * local_key - b
  internal::op::Sub(absl::MakeConstSpan(mac), absl::MakeConstSpan(b),
                    absl::MakeSpan(mac));
  // Pack
  internal::Pack(absl::MakeConstSpan(a), absl::MakeConstSpan(mac),
                 absl::MakeSpan(out));
}

void TrueCorrelation::RandomGet(absl::Span<internal::ATy> out) {
  const size_t num = out.size();
  std::vector<internal::PTy> c(num);
  // remote_a * key_ + remote_b = c
  vole_sender_->rsend(absl::MakeSpan(c));
  // Pack
  auto zeros = internal::op::Zeros(num);
  internal::Pack(absl::MakeConstSpan(zeros), absl::MakeConstSpan(c),
                 absl::MakeSpan(out));
}

void TrueCorrelation::RandomAuth(absl::Span<internal::ATy> out) {
  const size_t num = out.size();
  std::vector<internal::ATy> zeros(num);
  std::vector<internal::ATy> rands(num);
  if (ctx_->GetRank() == 0) {
    RandomSet(absl::MakeSpan(rands));
    RandomGet(absl::MakeSpan(zeros));
  } else {
    RandomGet(absl::MakeSpan(zeros));
    RandomSet(absl::MakeSpan(rands));
  }
  internal::op::Add(
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(zeros.data()),
                          2 * num),
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(rands.data()),
                          2 * num),
      absl::MakeSpan(reinterpret_cast<internal::PTy*>(out.data()), 2 * num));
}

void TrueCorrelation::ShuffleSet(absl::Span<const size_t> perm,
                                 absl::Span<internal::PTy> delta,
                                 size_t repeat) {
  auto conn = ctx_->GetConnection();
  const size_t batch_size = perm.size();
  const size_t full_size = delta.size();
  YACL_ENFORCE(full_size == batch_size * repeat);

  ot::OtHelper(ot_sender_, ot_receiver_).ShuffleSend(conn, perm, delta, repeat);
}

void TrueCorrelation::ShuffleGet(absl::Span<internal::PTy> a,
                                 absl::Span<internal::PTy> b, size_t repeat) {
  auto conn = ctx_->GetConnection();

  const size_t full_size = a.size();
  const size_t batch_size = a.size() / repeat;
  YACL_ENFORCE(full_size == b.size());
  YACL_ENFORCE(full_size == batch_size * repeat);

  ot::OtHelper(ot_sender_, ot_receiver_).ShuffleRecv(conn, a, b, repeat);
}

void TrueCorrelation::BdozTriple(absl::Span<internal::PTy> a,
                                  absl::Span<internal::PTy> a_pad,
                                  absl::Span<internal::PTy> peer_a_mac,
                                 absl::Span<internal::PTy> b,
                                 absl::Span<internal::PTy> b_pad,
                                 absl::Span<internal::PTy> peer_b_mac,
                                 absl::Span<internal::PTy> c,
                                 absl::Span<internal::PTy> c_pad,
                                 absl::Span<internal::PTy> peer_c_mac) {
  const size_t num = a.size();
  YACL_ENFORCE(num == a_pad.size());
  YACL_ENFORCE(num == peer_a_mac.size());
  YACL_ENFORCE(num == b.size());
  YACL_ENFORCE(num == b_pad.size());
  YACL_ENFORCE(num == peer_b_mac.size());
  YACL_ENFORCE(num == c.size());
  YACL_ENFORCE(num == c_pad.size());
  YACL_ENFORCE(num == peer_c_mac.size());

  auto conn = ctx_->GetConnection();
  internal::op::Rand(b);
  std::vector<internal::PTy> A(num);
  std::vector<internal::PTy> C(num);
  ot::OtHelper(ot_sender_, ot_receiver_)
      .BeaverTripleExtendWithChosenB(conn, a, b, c, absl::MakeSpan(A),
                                     absl::MakeSpan(C));

  // Auth both main and sacrifice values first, then run sacrifice check on
  // authenticated views.
  std::vector<internal::PTy> auth_input(num * 5);
  auto auth_input_span = absl::MakeSpan(auth_input);
  auto auth_input_a = auth_input_span.subspan(0 * num, num);
  auto auth_input_b = auth_input_span.subspan(1 * num, num);
  auto auth_input_c = auth_input_span.subspan(2 * num, num);
  auto auth_input_A = auth_input_span.subspan(3 * num, num);
  auto auth_input_C = auth_input_span.subspan(4 * num, num);
  memcpy(auth_input_a.data(), a.data(), num * sizeof(internal::PTy));
  memcpy(auth_input_b.data(), b.data(), num * sizeof(internal::PTy));
  memcpy(auth_input_c.data(), c.data(), num * sizeof(internal::PTy));
  memcpy(auth_input_A.data(), A.data(), num * sizeof(internal::PTy));
  memcpy(auth_input_C.data(), C.data(), num * sizeof(internal::PTy));

  std::vector<internal::PTy> auth_pad(num * 5);
  std::vector<internal::PTy> auth_peer_mac(num * 5);
  BdozAuth(absl::MakeConstSpan(auth_input), absl::MakeSpan(auth_pad),
           absl::MakeSpan(auth_peer_mac));

  auto auth_pad_span = absl::MakeConstSpan(auth_pad);
  auto auth_peer_mac_span = absl::MakeConstSpan(auth_peer_mac);
  auto auth_a_pad = auth_pad_span.subspan(0 * num, num);
  auto auth_b_pad = auth_pad_span.subspan(1 * num, num);
  auto auth_c_pad = auth_pad_span.subspan(2 * num, num);
  auto auth_A_pad = auth_pad_span.subspan(3 * num, num);
  auto auth_C_pad = auth_pad_span.subspan(4 * num, num);
  auto auth_peer_a_mac = auth_peer_mac_span.subspan(0 * num, num);
  auto auth_peer_b_mac = auth_peer_mac_span.subspan(1 * num, num);
  auto auth_peer_c_mac = auth_peer_mac_span.subspan(2 * num, num);
  auto auth_peer_A_mac = auth_peer_mac_span.subspan(3 * num, num);
  auto auth_peer_C_mac = auth_peer_mac_span.subspan(4 * num, num);

  CheckBdozTriple(a, b, c, absl::MakeConstSpan(A), absl::MakeConstSpan(C),
                  auth_a_pad, auth_peer_a_mac, auth_b_pad, auth_peer_b_mac,
                  auth_c_pad, auth_peer_c_mac, auth_A_pad, auth_peer_A_mac,
                  auth_C_pad, auth_peer_C_mac);

  memcpy(a_pad.data(), auth_pad.data(), num * sizeof(internal::PTy));
  memcpy(b_pad.data(), auth_pad.data() + num, num * sizeof(internal::PTy));
  memcpy(c_pad.data(), auth_pad.data() + 2 * num, num * sizeof(internal::PTy));
  memcpy(peer_a_mac.data(), auth_peer_mac.data(),
         num * sizeof(internal::PTy));
  memcpy(peer_b_mac.data(), auth_peer_mac.data() + num,
         num * sizeof(internal::PTy));
  memcpy(peer_c_mac.data(), auth_peer_mac.data() + 2 * num,
         num * sizeof(internal::PTy));
}

BdozTy TrueCorrelation::BdozTriple(size_t num) {
  BdozTy ret(num);
  BdozTriple(absl::MakeSpan(ret.a), absl::MakeSpan(ret.a_pad),
             absl::MakeSpan(ret.peer_a_mac), absl::MakeSpan(ret.b),
             absl::MakeSpan(ret.b_pad), absl::MakeSpan(ret.peer_b_mac),
             absl::MakeSpan(ret.c), absl::MakeSpan(ret.c_pad),
             absl::MakeSpan(ret.peer_c_mac));
  return ret;
}

void TrueCorrelation::CheckBdozTriple(absl::Span<const internal::PTy> a,
                                      absl::Span<const internal::PTy> b,
                                      absl::Span<const internal::PTy> c,
                                      absl::Span<const internal::PTy> A,
                                      absl::Span<const internal::PTy> C,
                                      absl::Span<const internal::PTy> a_pad,
                                      absl::Span<const internal::PTy> peer_a_mac,
                                      absl::Span<const internal::PTy> b_pad,
                                      absl::Span<const internal::PTy> peer_b_mac,
                                      absl::Span<const internal::PTy> c_pad,
                                      absl::Span<const internal::PTy> peer_c_mac,
                                      absl::Span<const internal::PTy> A_pad,
                                      absl::Span<const internal::PTy> peer_A_mac,
                                      absl::Span<const internal::PTy> C_pad,
                                      absl::Span<const internal::PTy> peer_C_mac) {
  const size_t num = c.size();
  YACL_ENFORCE(num == a.size());
  YACL_ENFORCE(num == b.size());
  YACL_ENFORCE(num == A.size());
  YACL_ENFORCE(num == C.size());
  YACL_ENFORCE(num == a_pad.size());
  YACL_ENFORCE(num == peer_a_mac.size());
  YACL_ENFORCE(num == b_pad.size());
  YACL_ENFORCE(num == peer_b_mac.size());
  YACL_ENFORCE(num == c_pad.size());
  YACL_ENFORCE(num == peer_c_mac.size());
  YACL_ENFORCE(num == A_pad.size());
  YACL_ENFORCE(num == peer_A_mac.size());
  YACL_ENFORCE(num == C_pad.size());
  YACL_ENFORCE(num == peer_C_mac.size());

  auto seed = ctx_->GetConnection()->SyncSeed();
  auto coef = internal::op::Rand(seed, num);

  auto rA = internal::op::Mul(A, absl::MakeConstSpan(coef));
  auto a_rA = internal::op::Add(a, absl::MakeConstSpan(rA));

  auto rC = internal::op::Mul(C, absl::MakeConstSpan(coef));
  auto c_rC = internal::op::Add(c, absl::MakeConstSpan(rC));

  auto rA_pad = internal::op::Mul(A_pad, absl::MakeConstSpan(coef));
  auto a_rA_pad = internal::op::Add(a_pad, absl::MakeConstSpan(rA_pad));
  auto rA_peer_mac = internal::op::Mul(peer_A_mac, absl::MakeConstSpan(coef));
  auto a_rA_peer_mac =
      internal::op::Add(peer_a_mac, absl::MakeConstSpan(rA_peer_mac));

  auto rC_pad = internal::op::Mul(C_pad, absl::MakeConstSpan(coef));
  auto c_rC_pad = internal::op::Add(c_pad, absl::MakeConstSpan(rC_pad));
  auto rC_peer_mac = internal::op::Mul(peer_C_mac, absl::MakeConstSpan(coef));
  auto c_rC_peer_mac =
      internal::op::Add(peer_c_mac, absl::MakeConstSpan(rC_peer_mac));

  auto opened_a_rA = OpenAndCheckBdoz(absl::MakeConstSpan(a_rA),
                                      absl::MakeConstSpan(a_rA_pad),
                                      absl::MakeConstSpan(a_rA_peer_mac));
  auto opened_c_rC = OpenAndCheckBdoz(absl::MakeConstSpan(c_rC),
                                      absl::MakeConstSpan(c_rC_pad),
                                      absl::MakeConstSpan(c_rC_peer_mac));

  auto local_prod =
      internal::op::Mul(absl::MakeConstSpan(opened_a_rA), absl::MakeConstSpan(b));
  auto local_prod_pad =
      internal::op::Mul(absl::MakeConstSpan(opened_a_rA), absl::MakeConstSpan(b_pad));
  auto local_prod_peer_mac = internal::op::Mul(
      absl::MakeConstSpan(opened_a_rA), absl::MakeConstSpan(peer_b_mac));
  auto opened_prod = OpenAndCheckBdoz(absl::MakeConstSpan(local_prod),
                                      absl::MakeConstSpan(local_prod_pad),
                                      absl::MakeConstSpan(local_prod_peer_mac));

  for (size_t i = 0; i < num; ++i) {
    YACL_ENFORCE(opened_c_rC[i] == opened_prod[i], "{} : sacrificed c is {}", i,
                 opened_c_rC[i].GetVal());
  }
}

std::vector<internal::PTy> TrueCorrelation::OpenAndCheckBdoz(
    absl::Span<const internal::PTy> val, absl::Span<const internal::PTy> pad,
    absl::Span<const internal::PTy> peer_mac) {
  const size_t num = val.size();
  YACL_ENFORCE(num == pad.size());
  YACL_ENFORCE(num == peer_mac.size());

  auto conn = ctx_->GetConnection();
  auto opened_val_buf = conn->Exchange(
      yacl::ByteContainerView(val.data(), num * sizeof(internal::PTy)));
  YACL_ENFORCE(static_cast<uint64_t>(opened_val_buf.size()) ==
               num * sizeof(internal::PTy));
  auto remote_val = absl::MakeConstSpan(
      reinterpret_cast<const internal::PTy*>(opened_val_buf.data()), num);

  auto opened_pad_buf = conn->Exchange(
      yacl::ByteContainerView(pad.data(), num * sizeof(internal::PTy)));
  YACL_ENFORCE(static_cast<uint64_t>(opened_pad_buf.size()) ==
               num * sizeof(internal::PTy));
  auto remote_pad = absl::MakeConstSpan(
      reinterpret_cast<const internal::PTy*>(opened_pad_buf.data()), num);

  auto opened_val = internal::op::Add(val, remote_val);
  auto opened_pad = internal::op::Add(pad, remote_pad);

  auto expected_mac = internal::op::Add(
      internal::op::ScalarMul(key_, absl::MakeConstSpan(opened_val)),
      absl::MakeConstSpan(opened_pad));
  auto actual_mac = internal::op::Add(
      internal::op::Add(
          internal::op::ScalarMul(key_, absl::MakeConstSpan(val)),
          absl::MakeConstSpan(pad)),
      absl::MakeConstSpan(peer_mac));

  for (size_t i = 0; i < num; ++i) {
    YACL_ENFORCE(expected_mac[i] == actual_mac[i],
                 "bad BDOZ auth when opening at index {}", i);
  }

  return opened_val;
}

void TrueCorrelation::BdozAuth(absl::Span<const internal::PTy> input,
                               absl::Span<internal::PTy> input_pad,
                               absl::Span<internal::PTy> peer_input_mac) {
  const size_t num = input.size();
  YACL_ENFORCE(num == input_pad.size());
  YACL_ENFORCE(num == peer_input_mac.size());

  auto recv_and_send_diff = [&] {
    std::vector<internal::PTy> a(num);
    std::vector<internal::PTy> b(num);
    vole_receiver_->rrecv(absl::MakeSpan(a), absl::MakeSpan(b));
    auto diff = internal::op::Sub(input, absl::MakeConstSpan(a));
    auto conn = ctx_->GetConnection();
    conn->SendAsync(
        conn->NextRank(),
        yacl::ByteContainerView(diff.data(),
                                diff.size() * sizeof(internal::PTy)),
        "BdozAuth:diff");
    memcpy(input_pad.data(), b.data(), num * sizeof(internal::PTy));
  };

  auto send_and_recv_diff = [&] {
    std::vector<internal::PTy> c(num);
    vole_sender_->rsend(absl::MakeSpan(c));

    auto conn = ctx_->GetConnection();
    auto recv_buf = conn->Recv(conn->NextRank(), "BdozAuth:diff");
    YACL_ENFORCE(static_cast<uint64_t>(recv_buf.size()) ==
                 num * sizeof(internal::PTy));
    auto diff = absl::MakeConstSpan(
        reinterpret_cast<const internal::PTy*>(recv_buf.data()), num);
    auto scaled_diff = internal::op::ScalarMul(key_, diff);
    internal::op::Add(absl::MakeConstSpan(c),
                      absl::MakeConstSpan(scaled_diff), peer_input_mac);
  };

  if (ctx_->GetRank() == 0) {
    recv_and_send_diff();
    send_and_recv_diff();
  } else {
    send_and_recv_diff();
    recv_and_send_diff();
  }
}

void TrueCorrelation::AuthSet(absl::Span<const internal::PTy> in,
                              absl::Span<internal::ATy> out) {
  RandomSet(out);
  auto [val, mac] = internal::Unpack(out);
  // val = in - val
  internal::op::Sub(absl::MakeConstSpan(in), absl::MakeConstSpan(val),
                    absl::MakeSpan(val));

  auto conn = ctx_->GetConnection();
  conn->SendAsync(
      conn->NextRank(),
      yacl::ByteContainerView(val.data(), val.size() * sizeof(internal::PTy)),
      "AuthSet");

  internal::op::Add(
      absl::MakeConstSpan(mac),
      absl::MakeConstSpan(internal::op::ScalarMul(key_, absl::MakeSpan(val))),
      absl::MakeSpan(mac));
  auto ret = internal::Pack(in, mac);
  memcpy(out.data(), ret.data(), out.size() * sizeof(internal::ATy));
}

void TrueCorrelation::AuthGet(absl::Span<internal::ATy> out) {
  RandomGet(out);
  // val = 0
  auto [val, mac] = internal::Unpack(out);

  auto conn = ctx_->GetConnection();
  auto recv_buf = conn->Recv(conn->NextRank(), "AuthSet");

  auto diff = absl::MakeSpan(reinterpret_cast<internal::PTy*>(recv_buf.data()),
                             out.size());

  internal::op::Add(absl::MakeConstSpan(mac),
                    absl::MakeConstSpan(internal::op::ScalarMul(key_, diff)),
                    absl::MakeSpan(mac));
  auto ret = internal::Pack(val, mac);
  memcpy(out.data(), ret.data(), ret.size() * sizeof(internal::ATy));
}

// Copy from A2P
std::vector<internal::PTy> TrueCorrelation::OpenAndCheck(
    absl::Span<const internal::ATy> in) {
  const size_t size = in.size();
  auto [val, mac] = internal::Unpack(absl::MakeSpan(in));
  auto conn = ctx_->GetConnection();
  auto val_bv =
      yacl::ByteContainerView(val.data(), size * sizeof(internal::PTy));
  std::vector<internal::PTy> real_val(size);

  auto buf = conn->Exchange(val_bv);
  internal::op::Add(
      absl::MakeConstSpan(reinterpret_cast<const internal::PTy*>(buf.data()),
                          size),
      absl::MakeConstSpan(val), absl::MakeSpan(real_val));

  // Generate Sync Seed After open Value
  auto sync_seed = conn->SyncSeed();
  auto coef = internal::op::Rand(sync_seed, size);
  // linear combination
  auto real_val_affine =
      internal::op::InPro(absl::MakeSpan(coef), absl::MakeSpan(real_val));
  auto mac_affine =
      internal::op::InPro(absl::MakeSpan(coef), absl::MakeSpan(mac));

  auto zero_mac = mac_affine - real_val_affine * key_;

  auto remote_mac_uint = conn->ExchangeWithCommit(zero_mac.GetVal());
  YACL_ENFORCE(zero_mac + internal::PTy(remote_mac_uint) ==
               internal::PTy::Zero());
  return real_val;
}

}  // namespace mcpsi
