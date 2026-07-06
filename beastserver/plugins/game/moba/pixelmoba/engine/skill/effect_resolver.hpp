#pragma once

#include "engine/world_state.hpp"
#include "engine/systems/combat_system.hpp"
#include "beast/platform/bizutil/config/store.hpp"
#include "beast/platform/engine/context/engine_context.hpp"
#include "biz_tables.hpp"
#include "effect.pb.h"
#include "skill.pb.h"
#include "skill_level.pb.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace beast::moba::pixel {
namespace biz = beast::biz::moba::pixel_moba;

// 解析 default_effect_ids JSON 数组(格式 "[6003,6004]" 或 "6003,6004" 或 "")
inline std::vector<std::uint32_t> parse_effect_ids(const std::string& s) {
    std::vector<std::uint32_t> ids;
    if (s.empty()) return ids;
    std::string cleaned;
    cleaned.reserve(s.size());
    for (char c : s) {
        if (c != '[' && c != ']' && c != ' ') cleaned += c;
    }
    std::istringstream iss(cleaned);
    std::string token;
    while (std::getline(iss, token, ',')) {
        if (token.empty()) continue;
        try { ids.push_back(static_cast<std::uint32_t>(std::stoul(token))); } catch (...) {}
    }
    return ids;
}

struct EffectContext {
    WorldState* world{nullptr};
    CombatSystem* combat{nullptr};
    beast::platform::engine::context::EngineContext* ctx{nullptr};
    beast::platform::EntityId caster_eid{0};
    beast::platform::EntityId target_eid{0};
    const biz::skill::SkillRowServer* skill_row{nullptr};
    const biz::skill_level::SkillLevelRowServer* level_row{nullptr};
    beast::platform::Tick tick{0};
};

// 按 effect_type 分支执行单个 effect
inline void resolve_effect(const EffectContext& ec, const biz::effect::EffectRowServer& eff) {
    if (ec.world == nullptr || ec.combat == nullptr) return;
    const auto& et = eff.effect_type();

    if (et == "buff_stun") {
        std::int32_t dur_ms = ec.level_row ? ec.level_row->buff_duration_ms() : 0;
        if (dur_ms <= 0) dur_ms = 1000;
        BuffData b;
        b.effect_id = eff.id();
        b.expire_tick = ec.tick + static_cast<beast::platform::Tick>(dur_ms / kTickMs);
        b.buff_flag = kBuffStunBit;
        ec.world->apply_buff(ec.target_eid, b, eff.stack_max() > 0 ? eff.stack_max() : 1);
    }
    else if (et == "buff_slow") {
        // formula 格式: "slow_ratio;duration_source",如 "0.3;$skill_level.buff_duration_ms"
        float slow_ratio = 0.3f;
        const auto& formula = eff.formula();
        auto sep = formula.find(';');
        if (sep != std::string::npos) {
            try { slow_ratio = std::stof(formula.substr(0, sep)); } catch (...) {}
        }
        std::int32_t dur_ms = ec.level_row ? ec.level_row->buff_duration_ms() : 0;
        if (dur_ms <= 0) dur_ms = 2000;
        BuffData b;
        b.effect_id = eff.id();
        b.expire_tick = ec.tick + static_cast<beast::platform::Tick>(dur_ms / kTickMs);
        b.buff_flag = kBuffSlowBit;
        const std::uint32_t smax = eff.stack_max() > 0 ? eff.stack_max() : 1;
        if (smax > 1) {
            // 叠层减速(寒冷):查现有 stacks,move_speed_mod = -ratio * new_stacks
            std::uint32_t new_stacks = 1;
            if (auto* existing = ec.world->find_buff_mut(ec.target_eid, eff.id())) {
                if (existing->expire_tick > ec.tick) {
                    new_stacks = (existing->stacks < smax) ? existing->stacks + 1 : smax;
                }
            }
            b.stacks = new_stacks;
            b.move_speed_mod = -slow_ratio * static_cast<float>(new_stacks);
        } else {
            b.move_speed_mod = -slow_ratio;  // 负值 = 减速
        }
        ec.world->apply_buff(ec.target_eid, b, smax);
    }
    else if (et == "buff_airborne") {
        // 击飞:设 kBuffAirborneBit,阻止一切行动(等同 stun 但语义不同)
        std::int32_t dur_ms = ec.level_row ? ec.level_row->buff_duration_ms() : 0;
        if (dur_ms <= 0) dur_ms = 1000;
        BuffData b;
        b.effect_id = eff.id();
        b.expire_tick = ec.tick + static_cast<beast::platform::Tick>(dur_ms / kTickMs);
        b.buff_flag = kBuffAirborneBit;
        ec.world->apply_buff(ec.target_eid, b, eff.stack_max() > 0 ? eff.stack_max() : 1);
    }
    else if (et == "buff_mark") {
        // 标记类 buff(破甲标记/震荡状态/强化普攻):无 CC 位,仅存储 effect_id 供 consumer 查找
        std::int32_t dur_ms = ec.level_row ? ec.level_row->buff_duration_ms() : 0;
        if (dur_ms <= 0) dur_ms = 3000;
        BuffData b;
        b.effect_id = eff.id();
        b.expire_tick = ec.tick + static_cast<beast::platform::Tick>(dur_ms / kTickMs);
        ec.world->apply_buff(ec.target_eid, b, eff.stack_max() > 0 ? eff.stack_max() : 1);
    }
    else if (et == "damage") {
        // 完整实现(供未来 data-driven 技能):当前内置技能自己做伤害,不走此分支
        if (ec.target_eid == 0 || ec.level_row == nullptr) return;
        std::int32_t damage = ec.level_row->damage_base();
        auto h_it = ec.world->heroes.find(ec.caster_eid);
        if (h_it != ec.world->heroes.end()) {
            damage += static_cast<std::int32_t>(ec.level_row->physical_scaling() * h_it->second.physical_attack);
            damage += static_cast<std::int32_t>(ec.level_row->magic_scaling() * h_it->second.magic_attack);
        }
        std::uint32_t dtype = 0;
        if (eff.element() == "magical" || eff.element() == "magic") dtype = 1;
        else if (eff.element() == "true") dtype = 2;
        ec.combat->apply_damage(ec.caster_eid, ec.target_eid, damage, dtype, 0, ec.tick);
    }
    else if (et == "heal") {
        if (ec.target_eid == 0 || ec.level_row == nullptr) return;
        std::int32_t heal = ec.level_row->heal_base();
        auto h_it = ec.world->heroes.find(ec.caster_eid);
        if (h_it != ec.world->heroes.end()) {
            heal += static_cast<std::int32_t>(ec.level_row->magic_scaling() * h_it->second.magic_attack);
        }
        auto t_it = ec.world->entities.find(ec.target_eid);
        if (t_it == ec.world->entities.end()) return;
        t_it->second.hp += heal;
        if (t_it->second.hp > t_it->second.max_hp) t_it->second.hp = t_it->second.max_hp;
    }
}

// 按 effect_id 查 effect 配表并解析
inline void resolve_effect_by_id(const EffectContext& ec, std::uint32_t effect_id) {
    if (ec.ctx == nullptr) return;
    const auto* store = ec.ctx->biz_config();
    if (!store) return;
    const auto* cfg = store->find<biz::effect::EffectServerConfig>(kEffectTableLogicalName);
    if (!cfg) return;
    for (const auto& row : cfg->rows()) {
        if (row.id() == effect_id) {
            resolve_effect(ec, row);
            return;
        }
    }
}

// 解析 skill.default_effect_ids,逐个 resolve(要求 ec.skill_row 非空)
inline void resolve_skill_effects(const EffectContext& ec) {
    if (ec.skill_row == nullptr) return;
    auto ids = parse_effect_ids(ec.skill_row->default_effect_ids());
    for (auto eid : ids) {
        resolve_effect_by_id(ec, eid);
    }
}

} // namespace beast::moba::pixel
