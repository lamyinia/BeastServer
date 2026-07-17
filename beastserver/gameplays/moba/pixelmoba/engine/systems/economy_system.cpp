#include "engine/systems/economy_system.hpp"

#include "biz_tables.hpp"
#include "engine/world_state.hpp"

#include "beast/platform/bizutil/config/store.hpp"
#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/context/engine_context.hpp"
#include "item.pb.h"
#include "match_rewards.pb.h"
#include "recipe.pb.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace beast::moba::pixel {
namespace biz = beast::biz::moba::pixel_moba;

namespace {

// 解析 item.stat_bonus 字符串,格式 "key=value;key=value"(兼容旧格式 "key:value,key:value")。
// 例:"physical_attack=15;magic_defense=5;move_speed_pct=0.15"
struct StatBonus {
    std::int32_t physical_attack{0};
    std::int32_t magic_attack{0};
    std::int32_t physical_defense{0};
    std::int32_t magic_defense{0};
    std::int32_t max_hp{0};
    std::int32_t max_mana{0};
    float hp_regen{0.f};
    float mana_regen{0.f};
    float move_speed_pct{0.f};
    float crit_rate{0.f};
    float crit_damage{0.f};
};

StatBonus parse_stat_bonus(const std::string& s) {
    StatBonus b;
    if (s.empty()) return b;
    // 分隔符:优先 ';' (xlsx map 规范),兼容 ',' (旧格式)
    std::string normalized = s;
    if (normalized.find(';') == std::string::npos) {
        std::replace(normalized.begin(), normalized.end(), ',', ';');
    }
    std::string token;
    std::istringstream iss(normalized);
    while (std::getline(iss, token, ';')) {
        // 键值分隔符:优先 '=' (xlsx 规范),兼容 ':' (旧格式)
        auto sep = token.find('=');
        if (sep == std::string::npos) sep = token.find(':');
        if (sep == std::string::npos) continue;
        const std::string key = token.substr(0, sep);
        const std::string val_str = token.substr(sep + 1);
        try {
            if (key == "physical_attack") b.physical_attack = std::stoi(val_str);
            else if (key == "magic_attack") b.magic_attack = std::stoi(val_str);
            else if (key == "physical_defense") b.physical_defense = std::stoi(val_str);
            else if (key == "magic_defense") b.magic_defense = std::stoi(val_str);
            else if (key == "max_hp") b.max_hp = std::stoi(val_str);
            else if (key == "max_mana") b.max_mana = std::stoi(val_str);
            else if (key == "hp_regen") b.hp_regen = std::stof(val_str);
            else if (key == "mana_regen") b.mana_regen = std::stof(val_str);
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

// 查 recipe 配表行(按成品 item_id)
const biz::recipe::RecipeRowServer* find_recipe(
    const beast::platform::bizutil::config::BizConfigStore& store, std::uint32_t item_id) {
    const auto* cfg = store.find<biz::recipe::RecipeServerConfig>(kRecipeTableLogicalName);
    if (!cfg) return nullptr;
    for (const auto& row : cfg->rows()) {
        if (row.id() == item_id) return &row;
    }
    return nullptr;
}

// 解析 "id1,id2,id3" → vector<uint32>
std::vector<std::uint32_t> parse_component_ids(const std::string& s) {
    std::vector<std::uint32_t> ids;
    std::string token;
    std::istringstream iss(s);
    while (std::getline(iss, token, ',')) {
        try {
            ids.push_back(static_cast<std::uint32_t>(std::stoul(token)));
        } catch (...) {
        }
    }
    return ids;
}

void apply_bonus(HeroData& h, const StatBonus& b, int sign) {
    h.equip_physical_attack += sign * b.physical_attack;
    h.equip_magic_attack += sign * b.magic_attack;
    h.equip_physical_defense += sign * b.physical_defense;
    h.equip_magic_defense += sign * b.magic_defense;
    h.equip_max_hp += sign * b.max_hp;
    h.equip_max_mana += sign * b.max_mana;
    h.equip_hp_regen += sign * b.hp_regen;
    h.equip_mana_regen += sign * b.mana_regen;
    h.equip_move_speed_pct += sign * b.move_speed_pct;
    h.equip_crit_rate += sign * b.crit_rate;
    h.equip_crit_damage += sign * b.crit_damage;
}

// 查找可堆叠的消耗品槽位(同 item_id 且 count < stack_max)
std::vector<HeroData::InventorySlot>::iterator find_stackable_slot(
    HeroData& h, std::uint32_t item_id, std::int32_t stack_max) {
    for (auto it = h.inventory.begin(); it != h.inventory.end(); ++it) {
        if (it->item_id == item_id && it->count < stack_max) return it;
    }
    return h.inventory.end();
}

// 查找含指定 item_id 的槽位(优先消耗品堆叠槽,否则第一个匹配)
std::vector<HeroData::InventorySlot>::iterator find_owned_slot(
    HeroData& h, std::uint32_t item_id) {
    for (auto it = h.inventory.begin(); it != h.inventory.end(); ++it) {
        if (it->item_id == item_id) return it;
    }
    return h.inventory.end();
}

// 填充 EquipChangeNotify:equipped_item_ids(仅装备)+ inventory_item_ids/counts(全量)
void fill_equip_notify(EquipChangeNotify& eq, const HeroData& h, const biz::item::ItemRowServer* (*find_item)(const beast::platform::bizutil::config::BizConfigStore&, std::uint32_t), const beast::platform::bizutil::config::BizConfigStore* store) {
    for (const auto& slot : h.inventory) {
        eq.add_inventory_item_ids(slot.item_id);
        eq.add_inventory_item_counts(static_cast<std::uint32_t>(slot.count));
        // equipped_item_ids 只含装备(is_consumable=false),供客户端外观显示
        if (store != nullptr) {
            const auto* item = find_item(*store, slot.item_id);
            if (item != nullptr && !item->is_consumable()) {
                for (std::int32_t i = 0; i < slot.count; ++i) eq.add_equipped_item_ids(slot.item_id);
            }
        }
    }
}

} // namespace

void EconomySystem::on_start(
    beast::platform::engine::context::EngineContext& ctx, WorldState& world) {
    ctx_ = &ctx;
    world_ = &world;
}

void EconomySystem::tick(beast::platform::Tick tick, beast::platform::TimestampMs /*dt_ms*/) {
    if (!world_ || !world_->match_started || world_->match_ended) return;

    // 每 30 tick(0.5s)应用一次 regen + 被动金币,降低 AttrSync 流量
    // (60Hz 每 tick 改 hp/mana/gold + mark_attr_dirty 流量过大,0.5s 一次客户端体感无差别)
    constexpr beast::platform::Tick kRegenIntervalTicks = 30;
    if (tick % kRegenIntervalTicks != 0) return;

    constexpr float kDtSec = static_cast<float>(kRegenIntervalTicks) / 60.f;   // 0.5s
    // 被动金币读 match_rewards 表(缺失兜底 5.f/秒)
    const auto* store = (ctx_ != nullptr) ? ctx_->biz_config() : nullptr;
    const biz::match_rewards::MatchRewardsRowServer* rewards = nullptr;
    if (store) {
        const auto* cfg = store->find<biz::match_rewards::MatchRewardsServerConfig>(kMatchRewardsTableLogicalName);
        if (cfg) {
            for (const auto& row : cfg->rows()) {
                if (row.id() == 1) { rewards = &row; break; }
            }
        }
    }
    const float passive_gold_per_sec = (rewards != nullptr) ? rewards->passive_gold_per_sec() : 5.f;

    for (auto& [eid, h] : world_->heroes) {
        auto e_it = world_->entities.find(eid);
        if (e_it == world_->entities.end()) continue;
        auto& e = e_it->second;
        if (e.hp <= 0) continue;   // 死亡英雄不回血/回蓝/不给被动金币(避免死后状态混乱)

        // hp/mana regen(clamp 到 max)
        if (h.hp_regen > 0.f && e.hp < e.max_hp) {
            e.hp = std::min(e.max_hp, e.hp + static_cast<std::int32_t>(h.hp_regen * kDtSec));
        }
        if (h.mana_regen > 0.f && h.mana < h.max_mana) {
            h.mana = std::min(h.max_mana, h.mana + static_cast<std::int32_t>(h.mana_regen * kDtSec));
        }
        // 被动金币
        h.gold += static_cast<std::int32_t>(passive_gold_per_sec * kDtSec);

        world_->mark_attr_dirty(eid);
    }
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

    // 合成路径:若该 item 有 recipe 行,走"补差价"模型(build_cost + 缺失子件价格)
    if (store != nullptr) {
        const auto* recipe = find_recipe(*store, cmd.item_id());
        if (recipe != nullptr) {
            const auto component_ids = parse_component_ids(recipe->component_item_ids());
            // 标记背包中已被占用的槽位索引(防同一槽位被多个子件消耗)
            std::vector<bool> consumed(h.inventory.size(), false);
            std::int32_t pay = recipe->build_cost();
            std::size_t owned_count = 0;
            for (auto comp_id : component_ids) {
                bool found = false;
                for (std::size_t i = 0; i < h.inventory.size(); ++i) {
                    if (consumed[i]) continue;
                    if (h.inventory[i].item_id == comp_id) {
                        consumed[i] = true;
                        found = true;
                        ++owned_count;
                        break;
                    }
                }
                if (!found) {
                    // 缺失子件:加其 price 到 pay(补差价)
                    const auto* comp_item = find_item_row(*store, comp_id);
                    if (comp_item != nullptr) pay += comp_item->price();
                }
            }
            // 校验金币
            if (h.gold < pay) {
                BuyItemResult r;
                r.set_item_id(cmd.item_id());
                r.set_success(false);
                r.set_error_msg("gold_not_enough");
                ctx_->send(player_id, "pixelmoba.buyresult", r);
                return;
            }
            // 背包空间:净变化 = +1(成品) - owned_count(已持有子件被消耗)
            // owned_count >= 1 时不占新槽(消耗 N 个加 1 个,net <= 0)
            // owned_count == 0 时(全购买)需 +1 新槽
            const bool needs_new_slot_for_craft = (owned_count == 0);
            if (needs_new_slot_for_craft && h.inventory.size() >= HeroData::kInventoryMaxSlots) {
                BuyItemResult r;
                r.set_item_id(cmd.item_id());
                r.set_success(false);
                r.set_error_msg("inventory_full");
                ctx_->send(player_id, "pixelmoba.buyresult", r);
                return;
            }
            // 结算:扣 pay、消耗已持有子件(apply_bonus -1)、加成品(apply_bonus +1)、recompute
            h.gold -= pay;
            // 从后往前删 consumed 槽位(避免索引漂移),同时减回 stat_bonus
            for (std::size_t i = h.inventory.size(); i-- > 0;) {
                if (!consumed[i]) continue;
                const auto* comp_item = find_item_row(*store, h.inventory[i].item_id);
                if (comp_item != nullptr) {
                    apply_bonus(h, parse_stat_bonus(comp_item->stat_bonus()), -1);
                }
                h.inventory.erase(h.inventory.begin() + static_cast<std::ptrdiff_t>(i));
            }
            // 缺失子件价格已计入 pay,玩家直接拿到成品(不加入缺失子件到背包)
            h.inventory.push_back(HeroData::InventorySlot{cmd.item_id(), 1});
            apply_bonus(h, parse_stat_bonus(item->stat_bonus()), +1);
            world_->recompute_hero_stats(pe_it->second);

            BuyItemResult r;
            r.set_item_id(cmd.item_id());
            r.set_success(true);
            ctx_->send(player_id, "pixelmoba.buyresult", r);

            EquipChangeNotify eq;
            eq.set_entity_id(static_cast<std::uint32_t>(pe_it->second));
            fill_equip_notify(eq, h, find_item_row, store);
            if (ctx_ && world_) {
                for (const auto& pid : ctx_->player_ids()) {
                    if (world_->is_entity_visible_to_player(pid, pe_it->second)) {
                        ctx_->send(pid, "pixelmoba.equipchange", eq);
                    }
                }
            }
            BEAST_LOG_INFO("economy craft ok player={} item={} pay={} gold_left={}",
                           player_id, cmd.item_id(), pay, h.gold);
            return;
        }
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

    // 背包检查:消耗品优先堆叠到已有槽位(count < stack_max);否则需新槽位
    bool needs_new_slot = true;
    if (item->is_consumable() && item->stack_max() > 1) {
        auto slot_it = find_stackable_slot(h, cmd.item_id(), item->stack_max());
        if (slot_it != h.inventory.end()) {
            needs_new_slot = false;
        }
    }
    if (needs_new_slot && h.inventory.size() >= HeroData::kInventoryMaxSlots) {
        BuyItemResult r;
        r.set_item_id(cmd.item_id());
        r.set_success(false);
        r.set_error_msg("inventory_full");
        ctx_->send(player_id, "pixelmoba.buyresult", r);
        return;
    }

    // 结算:扣 gold、加背包(堆叠或新槽)、累加 stat_bonus、重算属性
    h.gold -= item->price();
    if (needs_new_slot) {
        h.inventory.push_back(HeroData::InventorySlot{cmd.item_id(), 1});
    } else {
        auto slot_it = find_stackable_slot(h, cmd.item_id(), item->stack_max());
        slot_it->count++;
    }
    apply_bonus(h, parse_stat_bonus(item->stat_bonus()), +1);
    world_->recompute_hero_stats(pe_it->second);

    BuyItemResult r;
    r.set_item_id(cmd.item_id());
    r.set_success(true);
    ctx_->send(player_id, "pixelmoba.buyresult", r);

    // 广播装备变更(外观显示)— 按视野过滤,避免敌方在视野外获知我方装备列表
    EquipChangeNotify eq;
    eq.set_entity_id(static_cast<std::uint32_t>(pe_it->second));
    fill_equip_notify(eq, h, find_item_row, store);
    if (ctx_ && world_) {
        for (const auto& pid : ctx_->player_ids()) {
            if (world_->is_entity_visible_to_player(pid, pe_it->second)) {
                ctx_->send(pid, "pixelmoba.equipchange", eq);
            }
        }
    }

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

    // 校验:持有该 item(消耗品或装备)
    auto owned_it = find_owned_slot(h, cmd.item_id());
    if (owned_it == h.inventory.end()) {
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

    // 结算:消耗品 count--(归 0 移除槽位),装备直接移除槽位;减回 stat_bonus(1 件);退 gold(1 件)
    if (item->is_consumable() && owned_it->count > 1) {
        owned_it->count--;
    } else {
        h.inventory.erase(owned_it);
    }
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
    fill_equip_notify(eq, h, find_item_row, store);
    // 按视野过滤(同 buy):敌方在视野外不应获知装备变化
    if (ctx_ && world_) {
        for (const auto& pid : ctx_->player_ids()) {
            if (world_->is_entity_visible_to_player(pid, pe_it->second)) {
                ctx_->send(pid, "pixelmoba.equipchange", eq);
            }
        }
    }

    BEAST_LOG_INFO("economy sell ok player={} item={} refund={} gold_left={}",
                   player_id, cmd.item_id(), refund, h.gold);
}

} // namespace beast::moba::pixel
