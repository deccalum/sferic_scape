#include "cli/browse.h"

#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstdio>
#include <iostream>
#include <optional>
#include <qr/qr.hpp>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "io/media_repository.h"
#include "io/schema.h"
#include "ui/text.h"

namespace sferic::cli {

using AllDatabaseTables =
    std::tuple<io::schema::Media, io::schema::MediaLabels, io::schema::Labels,
               io::schema::DetectorRun, io::schema::MediaDetection, io::schema::Exemplar,
               io::schema::Log, io::schema::YtVideo, io::schema::ParametricModel>;
std::unordered_map<std::string_view, std::unique_ptr<ITableBrowser>> table_registry;

namespace {

template <typename T>
concept HasToString = requires(T v) {
  { to_string(v) } -> std::convertible_to<std::string_view>;
};

std::string fmt_part(int part, int parts) {
  if (parts <= 1) return "-";
  return std::to_string(part) + "/" + std::to_string(parts);
}

template <typename T>
void print_field(std::ostream& os, const T& value) {
  if constexpr (HasToString<T>)
    os << to_string(value);
  else
    os << value;
}

void print_field(std::ostream& os, const std::chrono::system_clock::time_point& tp) {
  os << std::chrono::floor<std::chrono::seconds>(tp);
}

template <typename T>
void print_field(std::ostream& os, const std::optional<T>& value) {
  if (value.has_value())
    print_field(os, *value);
  else
    os << "NULL";
}

template <typename TableT>
class TableBrowser : public ITableBrowser {
 public:
  explicit TableBrowser(qr::Connection& db) : db_(db) {}
  std::string_view get_name() const override { return TableT::table_name; }

  void fetch_and_display() override {
    auto query = std::apply(
        [](auto... cols) {
          return qr::select(cols...).template from<TableT>();
        },
        TableT::columns);

    auto rows = db_.select_rows(query, [](auto... vals) {
      return std::make_tuple(vals...);
    });

    std::apply(
        [](const auto&... cols) {
          ((std::cout << cols.column_name << '\t'), ...);
        },
        TableT::columns);
    std::cout << '\n';

    for (const auto& row : rows) {
      std::apply(
          [](const auto&... vals) {
            ((print_field(std::cout, vals), std::cout << '\t'), ...);
          },
          row);
      std::cout << '\n';
    }
    std::cout << rows.size() << " row(s)\n";
  }

 private:
  qr::Connection& db_;
};

class MediaBrowser : public ITableBrowser {
 public:
  explicit MediaBrowser(qr::Connection& db) : db_(db) {}
  std::string_view get_name() const override { return "media"; }

  void fetch_and_display() override {
    io::MediaRepository repo(db_);
    const std::vector<io::MediaSourceInfo> rows = repo.fetch_media_catalog();

    constexpr size_t kTitleW = 40;
    constexpr size_t kUrlW = 44;
    constexpr size_t kPartW = 6;
    constexpr size_t kLenW = 8;
    constexpr size_t kRoleW = 9;

    std::printf("%s %s %s %s %s\n", ui::pad("title", kTitleW).c_str(),
                ui::pad("url", kUrlW).c_str(), ui::pad("part", kPartW).c_str(),
                ui::pad("length", kLenW).c_str(), ui::pad("role", kRoleW).c_str());
    std::printf(
        "%s\n",
        std::string(kTitleW + 1 + kUrlW + 1 + kPartW + 1 + kLenW + 1 + kRoleW, '-').c_str());

    for (const io::MediaSourceInfo& r : rows) {
      const std::string title = r.title.empty() ? r.external_id : r.title;
      std::printf("%s %s %s %s %s\n", ui::pad(ui::trunc(title, kTitleW), kTitleW).c_str(),
                  ui::pad(ui::trunc(r.url, kUrlW), kUrlW).c_str(),
                  ui::pad(fmt_part(r.part, r.parts), kPartW).c_str(),
                  ui::pad(ui::fmt_len(r.length_s), kLenW).c_str(),
                  ui::pad(to_string(r.corpus_role), kRoleW).c_str());
    }
    std::printf("%zu row(s)  (ids / windows / rates: use advanced tools or DB)\n", rows.size());
  }

 private:
  qr::Connection& db_;
};

class ExemplarBrowser : public ITableBrowser {
 public:
  explicit ExemplarBrowser(qr::Connection& db) : db_(db) {}
  std::string_view get_name() const override { return "exemplar"; }

  void fetch_and_display() override {
    using namespace io::schema;
    io::MediaRepository repo(db_);

    const std::vector<io::MediaSourceInfo> catalog = repo.fetch_media_catalog();
    std::unordered_map<int64_t, const io::MediaSourceInfo*> by_id;
    by_id.reserve(catalog.size());
    for (const io::MediaSourceInfo& m : catalog) by_id[m.id] = &m;

    auto query = qr::select(Exemplar::id, Exemplar::source_media_id, Exemplar::start_s,
                            Exemplar::end_s, Exemplar::approved)
                     .from<Exemplar>()
                     .order_by(Exemplar::source_media_id);

    struct Row {
      int64_t id;
      int64_t media_id;
      double start_s;
      double end_s;
      std::optional<bool> approved;
    };
    auto exemplars = db_.select_rows(query, [](int64_t id, int64_t media_id, double start_s,
                                               double end_s, std::optional<bool> approved) {
      return Row{id, media_id, start_s, end_s, approved};
    });

    std::sort(exemplars.begin(), exemplars.end(), [&](const Row& a, const Row& b) {
      const io::MediaSourceInfo* ma = by_id.count(a.media_id) ? by_id[a.media_id] : nullptr;
      const io::MediaSourceInfo* mb = by_id.count(b.media_id) ? by_id[b.media_id] : nullptr;
      const std::string ga = ma ? ma->group : "";
      const std::string gb = mb ? mb->group : "";
      if (ga != gb) return ga < gb;
      const int pa = ma ? ma->part : 0;
      const int pb = mb ? mb->part : 0;
      if (pa != pb) return pa < pb;
      if (a.start_s != b.start_s) return a.start_s < b.start_s;
      return a.id < b.id;
    });

    constexpr size_t kTitleW = 36;
    constexpr size_t kUrlW = 40;
    constexpr size_t kPartW = 6;
    constexpr size_t kLenW = 8;
    constexpr size_t kStatusW = 8;

    std::printf("%s %s %s %s %s\n", ui::pad("title", kTitleW).c_str(),
                ui::pad("url", kUrlW).c_str(), ui::pad("part", kPartW).c_str(),
                ui::pad("length", kLenW).c_str(), ui::pad("status", kStatusW).c_str());
    std::printf(
        "%s\n",
        std::string(kTitleW + 1 + kUrlW + 1 + kPartW + 1 + kLenW + 1 + kStatusW, '-').c_str());

    for (const Row& e : exemplars) {
      const io::MediaSourceInfo* m = by_id.count(e.media_id) ? by_id[e.media_id] : nullptr;
      const std::string title = m ? (m->title.empty() ? m->external_id : m->title) : "?";
      const std::string url = m ? m->url : "?";
      const std::string part = m ? fmt_part(m->part, m->parts) : "-";
      const double len = e.end_s > e.start_s ? e.end_s - e.start_s : 0.0;
      const char* status = !e.approved.has_value() ? "pending"
                           : *e.approved           ? "approved"
                                                   : "rejected";
      std::printf("%s %s %s %s %s\n", ui::pad(ui::trunc(title, kTitleW), kTitleW).c_str(),
                  ui::pad(ui::trunc(url, kUrlW), kUrlW).c_str(), ui::pad(part, kPartW).c_str(),
                  ui::pad(ui::fmt_len(len), kLenW).c_str(), ui::pad(status, kStatusW).c_str());
    }
    std::printf("%zu row(s)\n", exemplars.size());
  }

 private:
  qr::Connection& db_;
};

}  // namespace

template <typename... Ts>
void register_tables(qr::Connection& db, std::tuple<Ts...>) {
  (..., (table_registry[Ts::table_name] = std::make_unique<TableBrowser<Ts>>(db)));
}

void init_database_browser(qr::Connection& db) {
  register_tables(db, AllDatabaseTables{});
  table_registry["media"] = std::make_unique<MediaBrowser>(db);
  table_registry["exemplar"] = std::make_unique<ExemplarBrowser>(db);
}

}  // namespace sferic::cli
