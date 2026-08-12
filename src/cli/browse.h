#pragma once

#include <memory>
#include <string_view>
#include <unordered_map>

namespace qr {
class Connection;
}

namespace sferic::cli {

struct ITableBrowser {
  virtual ~ITableBrowser() = default;
  virtual std::string_view get_name() const = 0;
  virtual void fetch_and_display() = 0;
};

extern std::unordered_map<std::string_view, std::unique_ptr<ITableBrowser>> table_registry;
void init_database_browser(qr::Connection& db);

}  // namespace sferic::cli
