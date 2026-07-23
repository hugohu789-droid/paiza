// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid

#include "core/candidate_engine.h"

#include <gtest/gtest.h>

#include <memory>

namespace paiza {
namespace {

// Configurable fake source for engine behaviour tests.
class FakeSource : public CandidateSource {
 public:
  FakeSource(std::wstring name, int priority, bool available,
             std::vector<Candidate> results)
      : name_(std::move(name)),
        priority_(priority),
        available_(available),
        results_(std::move(results)) {}

  std::wstring name() const override { return name_; }
  int priority() const override { return priority_; }
  bool IsAvailable() const override { return available_; }
  std::vector<Candidate> Query(const std::string&, size_t max) override {
    ++query_count_;
    std::vector<Candidate> out = results_;
    if (out.size() > max) out.resize(max);
    return out;
  }

  int query_count() const { return query_count_; }

 private:
  std::wstring name_;
  int priority_;
  bool available_;
  std::vector<Candidate> results_;
  int query_count_ = 0;
};

Candidate MakeCandidate(const wchar_t* text, int score = 0) {
  Candidate c;
  c.text = text;
  c.score = score;
  return c;
}

TEST(CandidateEngineTest, EmptyEngineReturnsNothing) {
  CandidateEngine engine;
  EXPECT_TRUE(engine.Query("abc", 9).empty());
}

TEST(CandidateEngineTest, EmptyInputReturnsNothing) {
  CandidateEngine engine;
  engine.AddSource(std::make_unique<FakeSource>(
      L"a", 10, true, std::vector<Candidate>{MakeCandidate(L"\x1820")}));
  EXPECT_TRUE(engine.Query("", 9).empty());
}

TEST(CandidateEngineTest, HigherPrioritySourceComesFirst) {
  CandidateEngine engine;
  engine.AddSource(std::make_unique<FakeSource>(
      L"low", 1, true, std::vector<Candidate>{MakeCandidate(L"low")}));
  engine.AddSource(std::make_unique<FakeSource>(
      L"high", 100, true, std::vector<Candidate>{MakeCandidate(L"high")}));

  const auto results = engine.Query("x", 9);
  ASSERT_EQ(results.size(), 2u);
  EXPECT_EQ(results[0].text, L"high");
  EXPECT_EQ(results[1].text, L"low");
}

TEST(CandidateEngineTest, UnavailableSourceIsSkipped) {
  CandidateEngine engine;
  engine.AddSource(std::make_unique<FakeSource>(
      L"offline", 100, false,
      std::vector<Candidate>{MakeCandidate(L"offline")}));
  EXPECT_TRUE(engine.Query("x", 9).empty());
}

TEST(CandidateEngineTest, DuplicatesAcrossSourcesAreMerged) {
  CandidateEngine engine;
  engine.AddSource(std::make_unique<FakeSource>(
      L"a", 100, true, std::vector<Candidate>{MakeCandidate(L"same")}));
  engine.AddSource(std::make_unique<FakeSource>(
      L"b", 50, true, std::vector<Candidate>{MakeCandidate(L"same"),
                                             MakeCandidate(L"other")}));
  const auto results = engine.Query("x", 9);
  ASSERT_EQ(results.size(), 2u);
  EXPECT_EQ(results[0].text, L"same");
  EXPECT_EQ(results[1].text, L"other");
}

TEST(CandidateEngineTest, MaxResultsIsHonoured) {
  CandidateEngine engine;
  std::vector<Candidate> many;
  for (int i = 0; i < 20; ++i) {
    many.push_back(MakeCandidate((L"c" + std::to_wstring(i)).c_str(), i));
  }
  engine.AddSource(std::make_unique<FakeSource>(L"a", 1, true, many));
  EXPECT_EQ(engine.Query("x", 5).size(), 5u);
}

TEST(CandidateEngineTest, SortsByScoreWithinSource) {
  CandidateEngine engine;
  engine.AddSource(std::make_unique<FakeSource>(
      L"a", 1, true,
      std::vector<Candidate>{MakeCandidate(L"lo", 1),
                             MakeCandidate(L"hi", 9)}));
  const auto results = engine.Query("x", 9);
  ASSERT_EQ(results.size(), 2u);
  EXPECT_EQ(results[0].text, L"hi");
}

}  // namespace
}  // namespace paiza
