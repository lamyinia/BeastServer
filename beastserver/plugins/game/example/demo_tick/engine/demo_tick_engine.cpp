#include "engine/demo_tick_engine.hpp"

#include "biz_tables.hpp"

#include "beast/platform/bizutil/config/store.hpp"
#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/context/engine_context.hpp"

#include "npc.pb.h"

#include <memory>

namespace beast::demo::tick {
namespace {

using NpcConfig = beast::biz::example::npc::NpcServerConfig;
using NpcRow = beast::biz::example::npc::NpcRowServer;

void load_sample_npc_from_biz(
    const beast::platform::engine::context::EngineContext& ctx,
    std::string& out_name,
    std::string& out_model) {
    const auto* store = ctx.biz_config();
    if (store == nullptr) {
        BEAST_LOG_WARN("demo_tick: bizconfig store unavailable");
        return;
    }
    if (!store->loaded()) {
        BEAST_LOG_WARN("demo_tick: bizconfig not loaded (check server.json bizconfig.enabled and export)");
        return;
    }

    if (!store->contains(kNpcTableLogicalName)) {
        BEAST_LOG_WARN("demo_tick: table {} not registered or missing", kNpcTableLogicalName);
        return;
    }

    const auto& elder = store->require_row_by_id<NpcConfig, NpcRow>(kNpcTableLogicalName, 1002);
    BEAST_LOG_INFO(
        "demo_tick: require_row_by_id(1002) -> index={} name={}",
        elder.index(),
        elder.name());

    const auto& blacksmith = store->require_row_by_index<NpcConfig, NpcRow>(
        kNpcTableLogicalName,
        "blacksmith1");
    BEAST_LOG_INFO(
        "demo_tick: require_row_by_index(blacksmith1) -> id={} name={}",
        blacksmith.id(),
        blacksmith.name());

    const auto& singer = store->require_row_by_index<NpcConfig, NpcRow>(kNpcTableLogicalName, "singer1");
    out_name = singer.name();
    out_model = singer.default_model();
}

} // namespace

void DemoTickEngine::on_start(beast::platform::engine::context::EngineContext& ctx) {
    load_sample_npc_from_biz(ctx, sample_npc_name_, sample_npc_model_);
}

void DemoTickEngine::on_tick(
    beast::platform::Tick tick,
    beast::platform::TimestampMs /*dt_ms*/) {
    tick_count_.fetch_add(1, std::memory_order_relaxed);

    if (tick % 20 != 0 || sample_npc_name_.empty()) {
        return;
    }

    BEAST_LOG_INFO(
        "demo_tick: tick={} singer1 name={} model={}",
        tick,
        sample_npc_name_,
        sample_npc_model_);
}

std::unique_ptr<DemoTickEngine> make_demo_tick_engine() {
    return std::make_unique<DemoTickEngine>();
}

} // namespace beast::demo::tick
