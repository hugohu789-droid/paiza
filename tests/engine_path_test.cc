// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
//
// Resolving the `engine` field is the one place where a text file decides
// which program runs, so these tests are the contract's §4 rule written out:
// a bare name inside the install directory, and nothing else. The rejections
// are enumerated rather than sampled, because each one is a way a profile
// could otherwise reach outside that directory.

#include "core/engine_path.h"

#include <gtest/gtest.h>

#include <string>

namespace paiza {
namespace {

#ifdef _WIN32
constexpr wchar_t kSep = L'\\';
#else
constexpr wchar_t kSep = L'/';
#endif

const std::wstring& InstallDir() {
  static const std::wstring dir = std::wstring(L"C") + L":" + kSep + L"paiza";
  return dir;
}

// Convenience: accepted, and the reason is left empty.
::testing::AssertionResult Accepts(const std::wstring& name) {
  std::string reason = "untouched";
  if (!IsAcceptableEngineName(name, &reason)) {
    return ::testing::AssertionFailure() << "rejected: " << reason;
  }
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult Rejects(const std::wstring& name) {
  std::string reason;
  if (IsAcceptableEngineName(name, &reason)) {
    return ::testing::AssertionFailure() << "accepted";
  }
  if (reason.empty()) {
    return ::testing::AssertionFailure() << "rejected without a reason";
  }
  return ::testing::AssertionSuccess();
}

TEST(EnginePath, AcceptsABareName) {
  EXPECT_TRUE(Accepts(L"zcode-stdio"));
  EXPECT_TRUE(Accepts(L"zcode-stdio.exe"));
  EXPECT_TRUE(Accepts(L"paiza_engine_v2"));
  EXPECT_TRUE(Accepts(L"engine.1.2"));
}

TEST(EnginePath, RejectsAnEmptyName) { EXPECT_TRUE(Rejects(L"")); }

// The core rule. Without it every other check is decoration.
TEST(EnginePath, RejectsPathSeparators) {
  EXPECT_TRUE(Rejects(L"bin/zcode-stdio"));
  EXPECT_TRUE(Rejects(std::wstring(L"bin") + kSep + L"zcode-stdio"));
  EXPECT_TRUE(Rejects(L"./zcode-stdio"));
}

TEST(EnginePath, RejectsTraversal) {
  EXPECT_TRUE(Rejects(L".."));
  EXPECT_TRUE(Rejects(L"."));
  EXPECT_TRUE(Rejects(L"../../../usr/bin/sh"));
  EXPECT_TRUE(Rejects(L"..\\..\\Windows\\system32\\cmd.exe"));
}

TEST(EnginePath, RejectsAbsolutePaths) {
  EXPECT_TRUE(Rejects(L"/usr/bin/sh"));
  EXPECT_TRUE(Rejects(L"/bin/sh"));
  EXPECT_TRUE(Rejects(L"C:\\Windows\\system32\\cmd.exe"));
  EXPECT_TRUE(Rejects(L"\\\\server\\share\\payload.exe"));
}

// A colon with no separator after it: "C:engine.exe" means the current
// directory *on drive C*, which is not the install directory.
TEST(EnginePath, RejectsADriveRelativeName) {
  EXPECT_TRUE(Rejects(L"C:engine.exe"));
  EXPECT_TRUE(Rejects(L"Z:"));
}

// "engine.exe:payload" names an alternate data stream on NTFS; writing one is
// how a second executable hides behind a legitimate filename.
TEST(EnginePath, RejectsAnAlternateDataStream) {
  EXPECT_TRUE(Rejects(L"zcode-stdio.exe:payload"));
}

// A command line would read this as an option, not as a file to run.
TEST(EnginePath, RejectsALeadingDash) {
  EXPECT_TRUE(Rejects(L"-c"));
  EXPECT_TRUE(Rejects(L"--version"));
}

TEST(EnginePath, RejectsControlCharacters) {
  EXPECT_TRUE(Rejects(std::wstring(L"zcode") + wchar_t(0x0A) + L"stdio"));
  EXPECT_TRUE(Rejects(std::wstring(L"zcode") + wchar_t(0x09)));
  EXPECT_TRUE(Rejects(std::wstring(L"zcode") + wchar_t(0x7F)));
}

TEST(EnginePath, RejectsQuotesAndWildcards) {
  EXPECT_TRUE(Rejects(L"zcode\" && calc"));
  EXPECT_TRUE(Rejects(L"zcode'"));
  EXPECT_TRUE(Rejects(L"zcode*"));
  EXPECT_TRUE(Rejects(L"zcode?"));
  EXPECT_TRUE(Rejects(L"zcode|calc"));
  EXPECT_TRUE(Rejects(L"zcode<in"));
  EXPECT_TRUE(Rejects(L"zcode>out"));
}

// Windows strips a trailing dot or space when opening a file, so a name
// ending in one resolves to a *different* file than the one checked here.
TEST(EnginePath, RejectsTrailingDotsAndSpaces) {
  EXPECT_TRUE(Rejects(L"zcode-stdio."));
  EXPECT_TRUE(Rejects(L"zcode-stdio "));
  EXPECT_TRUE(Rejects(L"zcode-stdio.exe."));
  EXPECT_TRUE(Rejects(L" zcode-stdio"));
}

// These open a device rather than a file, in any directory and in any case.
TEST(EnginePath, RejectsReservedDeviceNames) {
  EXPECT_TRUE(Rejects(L"CON"));
  EXPECT_TRUE(Rejects(L"con"));
  EXPECT_TRUE(Rejects(L"NUL"));
  EXPECT_TRUE(Rejects(L"nul.exe"));
  EXPECT_TRUE(Rejects(L"PRN"));
  EXPECT_TRUE(Rejects(L"AUX"));
  EXPECT_TRUE(Rejects(L"COM1"));
  EXPECT_TRUE(Rejects(L"lpt9"));
  EXPECT_TRUE(Rejects(L"Com3.txt"));
}

// The device check must not swallow ordinary names that merely start the same
// way, or an engine could not be called "console-engine".
TEST(EnginePath, AcceptsNamesThatOnlyResembleDeviceNames) {
  EXPECT_TRUE(Accepts(L"CONSOLE"));
  EXPECT_TRUE(Accepts(L"NULL"));
  EXPECT_TRUE(Accepts(L"COM0"));
  EXPECT_TRUE(Accepts(L"COM10"));
  EXPECT_TRUE(Accepts(L"LPT"));
  EXPECT_TRUE(Accepts(L"console-engine"));
}

TEST(EnginePath, RejectionExplainsItself) {
  std::string reason;
  EXPECT_FALSE(IsAcceptableEngineName(L"../sh", &reason));
  EXPECT_NE(reason.find("separator"), std::string::npos) << reason;
}

TEST(EnginePath, ToleratesANullReason) {
  EXPECT_TRUE(IsAcceptableEngineName(L"zcode-stdio", nullptr));
  EXPECT_FALSE(IsAcceptableEngineName(L"../sh", nullptr));
}

// --------------------------------------------------------------- resolve ---

TEST(EnginePath, ResolveJoinsOntoTheInstallDirectory) {
  std::string reason;
  const std::wstring path =
      ResolveEnginePath(InstallDir(), L"zcode-stdio", &reason);
  EXPECT_EQ(path, InstallDir() + kSep + L"zcode-stdio");
  EXPECT_TRUE(reason.empty()) << reason;
}

TEST(EnginePath, ResolveDoesNotDoubleASeparator) {
  const std::wstring dir = InstallDir() + kSep;
  const std::wstring path = ResolveEnginePath(dir, L"zcode-stdio", nullptr);
  EXPECT_EQ(path, dir + L"zcode-stdio");
}

TEST(EnginePath, ResolveNeedsAnInstallDirectory) {
  std::string reason;
  EXPECT_TRUE(ResolveEnginePath(L"", L"zcode-stdio", &reason).empty());
  EXPECT_FALSE(reason.empty());
}

TEST(EnginePath, ResolveReturnsNothingForARejectedName) {
  for (const std::wstring name :
       {std::wstring(L""), std::wstring(L".."), std::wstring(L"/bin/sh"),
        std::wstring(L"C:\\Windows\\cmd.exe"), std::wstring(L"-c"),
        std::wstring(L"NUL"), std::wstring(L"a:b")}) {
    std::string reason;
    EXPECT_TRUE(ResolveEnginePath(InstallDir(), name, &reason).empty())
        << "resolved a rejected name";
    EXPECT_FALSE(reason.empty()) << "resolved without a reason";
  }
}

// The property the whole file exists to establish: whatever comes back, if
// anything, is a direct child of the install directory.
TEST(EnginePath, ResolveAlwaysStaysInsideTheInstallDirectory) {
  for (const std::wstring name :
       {std::wstring(L"zcode-stdio"), std::wstring(L"zcode-stdio.exe"),
        std::wstring(L"CONSOLE"), std::wstring(L"engine.1.2"),
        std::wstring(L"/bin/sh"), std::wstring(L"../../bin/sh"),
        std::wstring(L"..\\cmd.exe"), std::wstring(L"C:cmd.exe")}) {
    const std::wstring path = ResolveEnginePath(InstallDir(), name, nullptr);
    if (path.empty()) continue;
    const std::wstring prefix = InstallDir() + kSep;
    ASSERT_EQ(path.compare(0, prefix.size(), prefix), 0);
    const std::wstring tail = path.substr(prefix.size());
    EXPECT_EQ(tail.find(L'/'), std::wstring::npos);
    EXPECT_EQ(tail.find(L'\\'), std::wstring::npos);
    EXPECT_EQ(tail, name);
  }
}

}  // namespace
}  // namespace paiza
