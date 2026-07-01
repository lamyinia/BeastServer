#include "engine/systems/economy_system.hpp"

#include "biz_tables.hpp"
#include "engine/world_state.hpp"

#include "beast/platform/bizutil/config/store.hpp"
#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/context/engine_context.hpp"
#include "item.pb.h"

#include <sstream>
#include <string>

namespace beast::moba::pixel {
namespace biz = beast::biz::moba::pixel_moba;

namespace {

// 解析 item.stat_bonus 字符串,格式 "key:value,key:value"。
// 例:"physical_attack:10,magic_defense:5,move_speed_pct:0.1"
struct StatBonus {
    std::int32_t physical_attack{0};
    std::int32_t magic_attack{0};
    std::int32_t physical_defense{0};
    std::int32_t magic_defense{0};
    float move_speed_pct{0.f};
    float crit_rate{0.f};
    float crit_damage{0.f};
};

StatBonus parse_stat_bonus(const std::string& s) {
    StatBonus b;
    std::string token;
    std::istringstream iss(s);
    while (std::getline(iss, token, ',')) {
        const auto colon = token.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = token.substr(0, colon);
        const std::string val_str = token.substr(colon + 1);
        try {
            if (key == "physical_attack") b.physical_attack = std::stoi(val_str);
            else if (key == "magic_attack") b.magic_attack = std::stoi(val_str);
            else if (key == "physical_defense") b.physical_defense = std::stoi(val_str);
            else if (key == "magic_defense") b.magic_defense = std::stoi(val_str);
            else if (key == "move_speed_pct") b.move_speed_pct = std::stof(val_str);
            else if (key == "crit_rate") b.crit_rate = std::stof(val_str);
            else if (key == "crit_damage") b.crit_damage = std::stof(val_str);
        } catch (...) {
        }
    }
    return b;
}

// 查 item 配表行
const biz::item::ItemRowServer* find_item_row(
    const beast::platform::bizutil::config::BizConfigStore& store, std::uint32_t item_id) {
    const auto* cfg = store.find<biz::item::ItemServerConfig>(kItemTableLogicalName);
    if (!cfg) return nullptr;
    for (const auto& row : cfg->rows()) {
        if (row.id() == item_id) return &row;
    }
    return nullptr;
}

void apply_bonus(HeroData& h, const StatBonus& b, int sign) {
    h.equip_physical_attack += sign * b.physical_attack;
    h.equip_magic_attack += sign * b.magic_attack;
    h.equip_physical_defense += sign * b.physical_defense;
    h.equip_magic_defense += sign * b.magic_defense;
    h.equip_move_speed_pct += sign * b.move_speed_pct;
    h.equip_crit_rate += sign * b.crit_rate;
    h.equip_crit_damage += sign * b.crit_damage;
}

} // namespace

void EconomySystem::on_start(
    beast::platform::engine::context::EngineContext& ctx, WorldState& world) {
    ctx_ = &ctx;
    world_ = &world;
}

void EconomySystem::tick(beast::platform::Tick /*tick*/, beast::platform::TimestampMs /*dt_ms*/) {
    // stub:后续被动金币增长、经验结算
}

void EconomySystem::consume(const beast::platform::PlayerId& player_id, const BuyItemCmd& cmd) {
    const auto* store = (ctx_ != nullptr) ? ctx_->biz_config() : nullptr;
    auto pe_it = world_->player_entities.find(player_id);
    auto h_it = (pe_it != world_->player_entities.end())
                    ? world_->heroes.find(pe_it->second)
                    : world_->heroes.end();

    // 校验:英雄存在
    if (h_it == world_->heroes.end()) {
        BuyItemResult r;
        r.set_item_id(cmd.item_id());
        r.set_success(false);
        r.set_error_msg("no_hero");
        ctx_->send(player_id, "pixelmoba.buyresult", r);
        return;
    }
    auto& h = h_it->second;

    // 校验:item 存在
    const biz::item::ItemRowServer* item = (store != nullptr) ? find_item_row(*store, cmd.item_id()) : nullptr;
    if (item == nullptr) {
        BuyItemResult r;
        r.set_item_id(cmd.item_id());
        r.set_success(false);
        r.set_error_msg("item_not_found");
        ctx_->send(player_id, "pixelmoba.buyresult", r);
        return;
    }

    // 校验:金币足够
    if (h.gold < item->price()) {
        BuyItemResult r;
        r.set_item_id(cmd.item_id());
        r.set_success(false);
        r.set_error_msg("gold_not_enough");
        ctx_->send(player_id, "pixelmoba.buyresult", r);
        return;
    }

    // 结算:扣 gold、加装备、累加 stat_bonus、重算属性
    h.gold -= item->price();
    h.equipped_item_ids.push_back(cmd.item_id());
    apply_bonus(h, parse_stat_bonus(item->stat_bonus()), +1);
    world_->recompute_hero_stats(pe_it->second);

    BuyItemResult r;
    r.set_item_id(cmd.item_id());
    r.set_success(true);
    ctx_->send(player_id, "pixelmoba.buyresult", r);

    // 广播装备变更(外观显示)
    EquipChangeNotify eq;
    eq.set_entity_id(static_cast<std::uint32_t>(pe_it->second));
    for (auto id : h.equipped_item_ids) eq.add_equipped_item_ids(id);
    ctx_->broadcast("pixelmoba.equipchange", eq);

    BEAST_LOG_INFO("economy buy ok player={} item={} price={} gold_left={}",
                   player_id, cmd.item_id(), item->price(), h.gold);
}

void EconomySystem::consume(const beast::platform::PlayerId& player_id, const SellItemCmd& cmd) {
    const auto* store = (ctx_ != nullptr) ? ctx_->biz_config() : nullptr;
    auto pe_it = world_->player_entities.find(player_id);
    auto h_it = (pe_it != world_->player_entities.end())
                    ? world_->heroes.find(pe_it->second)
                    : world_->heroes.end();

    if (h_it == world_->heroes.end()) {
        SellItemResult r;
        r.set_item_id(cmd.item_id());
        r.set_success(false);
        r.set_error_msg("no_hero");
        ctx_->send(player_id, "pixelmoba.sellresult", r);
        return;
    }
    auto& h = h_it->second;

    // 校验:持有该装备
    auto owned_it = std::find(h.equipped_item_ids.begin(), h.equipped_item_ids.end(), cmd.item_id());
    if (owned_it == h.equipped_item_ids.end()) {
        SellItemResult r;
        r.set_item_id(cmd.item_id());
        r.set_success(false);
        r.set_error_msg("not_owned");
        ctx_->send(player_id, "pixelmoba.sellresult", r);
        return;
    }

    const biz::item::ItemRowServer* item = (store != nullptr) ? find_item_row(*store, cmd.item_id()) : nullptr;
    if (item == nullptr) {
        SellItemResult r;
        r.set_item_id(cmd.item_id());
        r.set_success(false);
        r.set_error_msg("item_not_found");
        ctx_->send(player_id, "pixelmoba.sellresult", r);
        return;
    }

    // 结算:移除装备、减回 stat_bonus、退 gold(sell_price_pct * price)、重算属性
    h.equipped_item_ids.erase(owned_it);
    apply_bonus(h, parse_stat_bonus(item->stat_bonus()), -1);
    const std::int32_t refund = static_cast<std::int32_t>(item->price() * item->sell_price_pct());
    h.gold += refund;
    world_->recompute_hero_stats(pe_it->second);

    SellItemResult r;
    r.set_item_id(cmd.item_id());
    r.set_success(true);
    r.set_refund_gold(refund);
    ctx_->send(player_id, "pixelmoba.sellresult", r);

    EquipChangeNotify eq;
    eq.set_entity_id(static_cast<std::uint32_t>(pe_it->second));
    for (auto id : h.equipped_item_ids) eq.add_equipped_item_ids(id);
    ctx_->broadcast("pixelmoba.equipchange", eq);

    BEAST_LOG_INFO("economy sell ok player={} item={} refund={} gold_left={}",
                   player_id, cmd.item_id(), refund, h.gold);
}

} // namespace beast::moba::pixel
