// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/GameVaultCamera.h"

#include <atomic>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

#include <fmt/format.h>

#include "Common/FileUtil.h"
#include "Common/IOFile.h"
#include "Common/Logging/Log.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using CamSocket = SOCKET;
static constexpr CamSocket BAD_SOCKET = INVALID_SOCKET;
static void CloseCamSocket(CamSocket s)
{
  closesocket(s);
}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using CamSocket = int;
static constexpr CamSocket BAD_SOCKET = -1;
static void CloseCamSocket(CamSocket s)
{
  close(s);
}
#endif

namespace Triforce::GameVaultCamera
{
namespace
{
std::mutex s_mutex;
std::thread s_thread;
std::atomic<bool> s_stop{false};
CamSocket s_listen = BAD_SOCKET;
u16 s_port = 0;
std::vector<u8> s_jpeg;

void LoadPicture()
{
  const std::string path = File::GetSysDirectory() + "Resources/default_camera_feed.jpg";
  File::IOFile file(path, "rb");
  s_jpeg.assign(file.IsOpen() ? file.GetSize() : 0, 0);
  if (s_jpeg.empty() || !file.ReadBytes(s_jpeg.data(), s_jpeg.size()))
  {
    WARN_LOG_FMT(AMMEDIABOARD, "GV camera: no picture at {}; serving an empty image", path);
    s_jpeg.clear();
  }
}

void SendAll(CamSocket s, const char* data, size_t size)
{
  while (size)
  {
    const int sent = send(s, data, int(std::min<size_t>(size, 1 << 16)), 0);
    if (sent <= 0)
      return;
    data += sent;
    size -= size_t(sent);
  }
}

void HandleClient(CamSocket client)
{
  // Read the request head (small); the path does not matter, every GET gets the picture.
  // Give up waiting after 2 s and answer anyway, so a request that never ends can't stall us.
#ifdef _WIN32
  const DWORD timeout_ms = 2000;
#else
  const timeval timeout_ms{2, 0};
#endif
  setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms),
             sizeof(timeout_ms));
  char buf[2048];
  std::string request;
  while (request.find("\r\n\r\n") == std::string::npos && request.size() < 8192)
  {
    const int got = recv(client, buf, sizeof(buf), 0);
    if (got <= 0)
      break;
    request.append(buf, size_t(got));
  }
  const std::string_view first_line = std::string_view(request).substr(0, request.find('\r'));
  NOTICE_LOG_FMT(AMMEDIABOARD, "GV camera: request ({} bytes): {}", request.size(), first_line);

  // One buffer, header and picture together, laid out like Dolphin PR #14393's server: the game
  // reads the reply in one go, and a header sent on its own was not enough (still CAMERA ERROR).
  const std::string head = fmt::format("HTTP/1.0 200 OK\r\nServer: Dolphin\r\n"
                                       "Date: Thu, 01 Jan 2026 00:00:00 GMT\r\n"
                                       "Content-Length: {}\r\nContent-Type: image/jpeg\r\n\r\n",
                                       s_jpeg.size());
  std::vector<char> reply(head.begin(), head.end());
  reply.insert(reply.end(), s_jpeg.begin(), s_jpeg.end());
  SendAll(client, reply.data(), reply.size());
  NOTICE_LOG_FMT(AMMEDIABOARD, "GV camera: sent {} bytes", reply.size());
#ifdef _WIN32
  shutdown(client, SD_SEND);
#else
  shutdown(client, SHUT_WR);
#endif
  CloseCamSocket(client);
}

void Serve(CamSocket listener)
{
  while (!s_stop)
  {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(listener, &set);
    timeval tv{0, 200 * 1000};
    if (select(int(listener) + 1, &set, nullptr, nullptr, &tv) <= 0)
      continue;
    const CamSocket client = accept(listener, nullptr, nullptr);
    if (client == BAD_SOCKET)
      continue;
    NOTICE_LOG_FMT(AMMEDIABOARD, "GV camera: client connected");
    HandleClient(client);  // one picture at a time is plenty: the game asks about once a second
  }
}
}  // namespace

void Start()
{
  std::lock_guard lock(s_mutex);
  if (s_thread.joinable())
    return;

#ifdef _WIN32
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

  LoadPicture();

  const CamSocket listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == BAD_SOCKET)
  {
    ERROR_LOG_FMT(AMMEDIABOARD, "GV camera: socket() failed");
    return;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;  // any free port
  socklen_t len = sizeof(addr);
  if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
      listen(listener, 8) != 0 ||
      getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len) != 0)
  {
    ERROR_LOG_FMT(AMMEDIABOARD, "GV camera: could not listen on 127.0.0.1");
    CloseCamSocket(listener);
    return;
  }

  s_listen = listener;
  s_port = ntohs(addr.sin_port);
  s_stop = false;
  s_thread = std::thread(Serve, listener);
  NOTICE_LOG_FMT(AMMEDIABOARD, "GV camera: serving a still picture ({} bytes) on 127.0.0.1:{}",
                 s_jpeg.size(), s_port);
}

void Stop()
{
  std::lock_guard lock(s_mutex);
  if (!s_thread.joinable())
    return;
  s_stop = true;
  s_thread.join();
  CloseCamSocket(s_listen);
  s_listen = BAD_SOCKET;
  s_port = 0;
}

std::optional<std::string> GetRedirection()
{
  std::lock_guard lock(s_mutex);
  if (!s_port)
    return std::nullopt;
  return fmt::format("192.168.29.104-107:80=127.0.0.1:{}", s_port);
}
}  // namespace Triforce::GameVaultCamera
