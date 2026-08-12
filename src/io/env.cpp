#include "io/env.h"

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>

namespace sferic::io::env {

std::string require(const char* key) {
  const char* v = std::getenv(key);
  return v ? std::string(v) : throw std::runtime_error(std::string("missing env var: ") + key);
}

std::filesystem::path resolve(const std::filesystem::path& dotenv) {
  if (dotenv.is_absolute()) return dotenv;
  for (std::filesystem::path dir = std::filesystem::current_path();; dir = dir.parent_path()) {
    if (std::filesystem::exists(dir / dotenv)) return dir / dotenv;
    if (dir == dir.parent_path()) return dotenv;
  }
}

void load(const std::filesystem::path& dotenv) {
  std::ifstream f(resolve(dotenv));
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    ::setenv(line.substr(0, eq).c_str(), line.substr(eq + 1).c_str(), 0);
  }
}

const std::string& database_url() {
  static const std::string url = require("DATABASE_URL");
  return url;
}

const std::string& python_interpreter() {
  static const std::string py = require("SFERIC_PYTHON");
  return py;
}

bool flag(const char* key) {
  const char* v = std::getenv(key);
  if (!v) return false;
  const std::string s(v);
  return s == "1" || s == "true" || s == "yes" || s == "on";
}

}  // namespace sferic::io::env
