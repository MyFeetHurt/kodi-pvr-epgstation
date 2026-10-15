// Captured EPGStation answers under tests/fixtures/<set>/.

#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

// "dev/channels.json". tests/fixtures/prod (the author's production server) stays private, so
// tests that need it are skipped in the public repository.
inline std::string ReadFile(const std::string& path)
{
  if (path.rfind("prod/", 0) == 0 && !std::filesystem::exists(std::string(FIXTURES_DIR) + "/prod"))
    SKIP("no production fixtures");
  std::ifstream in(std::string(FIXTURES_DIR) + "/" + path);
  REQUIRE(in.good());
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
