// Copyright (c) 2026 Skitzelles - part of Dolphin for Game Vault. Licensed like the rest of Dolphin
// (GPL-2.0-or-later).
//
// Test hook for checking the menus on a console without a controller in reach: a file gvtest_keys.txt
// uploaded into the app's LocalState (Device Portal) holds button presses, which the menus then see as
// pressed, in the menus and (Xbox pads, 1.1.9.127+) in games; "A&B,500" holds both. "wait,8000;Menu,200;Pad S,150;Button A,150" = wait 8 s, hold Menu 0.2 s, ... (names as the
// controller's inputs: Button A/B/X/Y, Menu, View, Pad N/S/E/W, Bumper L/R, Trigger L/R). Between two presses
// nothing is held for 0.25 s. The file is read once and deleted; a new one can follow at any time.

#pragma once

#include <chrono>
#include <functional>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace GvTest
{
struct State
{
  std::mutex lock;
  std::string watch_path;
  std::vector<std::pair<std::string, int>> steps;  // input name ("" = wait), milliseconds
  std::chrono::steady_clock::time_point start, last_poll;
  std::function<void(const std::string&)> log;
  size_t logged_step = SIZE_MAX;
};

inline State& Get()
{
  static State s;
  return s;
}

// called once by the frontend with the file to look for
inline void Watch(const std::string& path, std::function<void(const std::string&)> log = nullptr)
{
  std::lock_guard<std::mutex> l(Get().lock);
  Get().watch_path = path;
  Get().log = std::move(log);
}

inline void PollLocked(State& s)
{
  const auto now = std::chrono::steady_clock::now();
  if (s.watch_path.empty() || now - s.last_poll < std::chrono::milliseconds(500))
    return;
  s.last_poll = now;
  std::error_code ec;
  if (!std::filesystem::exists(s.watch_path, ec))
    return;
  std::string text;
  {
    std::ifstream f(s.watch_path);
    std::stringstream ss;
    ss << f.rdbuf();
    text = ss.str();
  }
  std::filesystem::remove(s.watch_path, ec);
  s.steps.clear();
  std::stringstream items(text);
  std::string item;
  while (std::getline(items, item, ';'))
  {
    const size_t comma = item.rfind(',');
    std::string name = comma == std::string::npos ? item : item.substr(0, comma);
    int ms = comma == std::string::npos ? 150 : std::atoi(item.c_str() + comma + 1);
    while (!name.empty() && (name.back() == '\r' || name.back() == '\n' || name.back() == ' '))
      name.pop_back();
    while (!name.empty() && (name.front() == '\r' || name.front() == '\n' || name.front() == ' '))
      name.erase(0, 1);
    if (name == "wait")
      name.clear();
    s.steps.emplace_back(name, ms);
    if (!name.empty())
      s.steps.emplace_back("", 250);
  }
  s.start = now;
  s.logged_step = SIZE_MAX;
  if (s.log)
    s.log("test keys: " + std::to_string(s.steps.size()) + " steps: " + text);
}

// true while a script runs (the menus then also listen when no controller is connected)
inline bool Active()
{
  State& s = Get();
  std::lock_guard<std::mutex> l(s.lock);
  PollLocked(s);
  return !s.steps.empty();
}

// true while the script holds this input
inline bool Pressed(const std::string& name)
{
  State& s = Get();
  std::lock_guard<std::mutex> l(s.lock);
  PollLocked(s);
  if (s.steps.empty())
    return false;
  long long t = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s.start)
                    .count();
  for (size_t i = 0; i < s.steps.size(); i++)
  {
    const auto& [step, ms] = s.steps[i];
    if (t < ms)
    {
      if (s.log && i != s.logged_step && !step.empty())
        s.log("test keys: holding " + step);
      s.logged_step = i;
      // "Button A&Left X-" holds both
      if (step == name)
        return true;
      for (size_t at = 0; (at = step.find(name, at)) != std::string::npos; at += name.size())
        if ((at == 0 || step[at - 1] == '&') &&
            (at + name.size() == step.size() || step[at + name.size()] == '&'))
          return true;
      return false;
    }
    t -= ms;
  }
  s.steps.clear();  // finished
  return false;
}
}  // namespace GvTest
