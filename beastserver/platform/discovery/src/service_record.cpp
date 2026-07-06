#include "beast/platform/discovery/service_record.hpp"

#include "beast/platform/core/config/server_config.hpp"

#include <nlohmann/json.hpp>

#include <sstream>
#include <vector>

namespace beast::platform::discovery {

std::string ServiceRecord::build_key() const {
    if (version.empty()) {
        return domain + "/" + addr;
    }
    return domain + "/" + version + "/" + addr;
}

std::string ServiceRecord::to_json() const {
    nlohmann::json j;
    j["domain"] = domain;
    j["addr"] = addr;
    j["weight"] = weight;
    j["version"] = version;
    j["ttl"] = ttl;
    j["load"] = load;
    j["nodeID"] = node_id;
    return j.dump();
}

std::optional<ServiceRecord> ServiceRecord::from_json(const std::string &json) {
    try {
        const auto j = nlohmann::json::parse(json);
        ServiceRecord record;
        record.domain = j.at("domain").get<std::string>();
        record.addr = j.at("addr").get<std::string>();
        record.weight = j.at("weight").get<int>();
        record.version = j.at("version").get<std::string>();
        record.ttl = j.at("ttl").get<int>();
        record.load = j.at("load").get<double>();

        if (j.contains("nodeID")) {
            record.node_id = j.at("nodeID").get<std::string>();
        } else if (j.contains("node_id")) {
            record.node_id = j.at("node_id").get<std::string>();
        }
        return record;
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

std::optional<ServiceRecord> ServiceRecord::parse_key(const std::string &key) {
    std::vector<std::string> parts;
    std::stringstream ss(key);
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }

    if (parts.size() == 2) {
        ServiceRecord record;
        record.domain = parts[0];
        record.addr = parts[1];
        return record;
    }
    if (parts.size() == 3) {
        ServiceRecord record;
        record.domain = parts[0];
        record.version = parts[1];
        record.addr = parts[2];
        return record;
    }
    return std::nullopt;
}

ServiceRecord make_service_record(const beast::platform::core::config::ServerConfig &server) {
    const auto &reg = server.etcd.registration;
    ServiceRecord record;
    record.domain = reg.domain;
    record.addr = reg.addr;
    record.version = reg.version;
    record.weight = reg.weight;
    record.ttl = reg.ttl;
    record.node_id = server.node_id;
    record.load = 1.0;
    return record;
}

} // namespace beast::platform::discovery
