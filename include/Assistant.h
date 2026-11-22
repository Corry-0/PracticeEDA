#pragma once
#include "Digital.h"
#include "json.hpp"
#include <atomic>

namespace eda::logic::ai {
using Json = nlohmann::json;
struct Snapshot {
    Project project;
    std::string definition, scope;
    std::set<std::string> selection;
    Json objects, diagnostics;
};
Snapshot capture(const Project &, const std::string &definition, const std::string &scope,
                 const std::set<std::string> &, const Simulator &);
struct Proposal {
    Project project;
    std::string definition, summary;
};
struct Reply {
    std::string text;
    std::optional<Proposal> proposal;
    std::set<std::string> references;
};
using Transport = std::function<Json(const Json &)>;
Json schemas();
Reply converse(const Snapshot &, Json messages, const Transport &, const std::atomic_bool &cancelled);
Json cloudRequest(const std::string &url, const std::string &model, const std::string &key, Json payload,
                  const std::atomic_bool &cancelled);
} // namespace eda::logic::ai
