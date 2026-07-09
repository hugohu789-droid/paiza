// Copyright 2026 Paiza. All rights reserved.

#include "ipc/ime_client.h"

#include <windows.h>

#include "common/logging.h"

namespace paiza::ipc {

bool ImeClient::QueryCandidates(const std::string& latin_input,
                                uint32_t max_results,
                                std::vector<Candidate>* candidates) {
  candidates->clear();
  QueryRequest request;
  request.latin_input = latin_input;
  request.max_results = max_results;

  const auto reply = client_.Transact(EncodeQueryRequest(request));
  if (!reply.has_value()) return false;
  const auto response = DecodeQueryResponse(*reply);
  if (!response.has_value()) return false;
  *candidates = response->candidates;
  return true;
}

bool ImeClient::ShowSettings() {
  return client_.Send({MessageType::kShowSettings, {}});
}

bool ImeClient::IsServiceRunning() {
  const auto reply = client_.Transact({MessageType::kPing, {}});
  return reply.has_value() && reply->type == MessageType::kPingReply;
}

void ImeClient::StartServiceProcess(const std::wstring& directory) {
  std::wstring exe = directory;
  if (!exe.empty() && exe.back() != L'\\') exe += L'\\';
  exe += L"PaizaService.exe";

  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process = {};
  std::wstring command_line = L"\"" + exe + L"\"";
  if (::CreateProcessW(exe.c_str(), command_line.data(), nullptr, nullptr,
                       FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                       &process)) {
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    PAIZA_LOG_INFO(L"Launched candidate service: " + exe);
  } else {
    // Expected in sandboxed hosts; the TSF module falls back to its
    // built-in transliterator.
    Logf(LogLevel::kWarning, L"CreateProcess(%s) failed: %lu", exe.c_str(),
         ::GetLastError());
  }
}

}  // namespace paiza::ipc
