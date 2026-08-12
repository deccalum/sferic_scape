#pragma once

#include <filesystem>
#include <string>

namespace sferic::io::env {

void load(const std::filesystem::path& dotenv = ".env");
const std::string& database_url();
const std::string& python_interpreter();
bool flag(const char* key);

}  // namespace sferic::io::env
