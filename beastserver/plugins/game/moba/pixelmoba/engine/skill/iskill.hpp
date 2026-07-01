#pragma once

#include "beast/platform/core/types.hpp"
#include "beast/platform/bizutil/math/vector/vec2.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace beast::platform::engine::context {
class EngineContext;
}

namespace beast::biz::moba::pixel_moba::skill {
class SkillRowServer;
} // namespace beast::biz::moba::pixel_moba::skill

namespace beast::biz::moba::pixel_moba::skill_level {
class SkillLevelRowServer;
} // namespace beast::biz::moba::pixel_moba::skill_level

namespace beast::moba::pixel {

class CombatSystem;
struct WorldState;

using beast::platform::bizutil::math::Vec2f;

// 技能释放上下文:CombatSystem 在 consume(CastCmd) 校验通过后构造,
// 传入技能逻辑执行所需的一切。ISkill::cast() 通过此结构访问世界状态、
// 配表、结算伤害与施加 buff,无需自行查 CD/蓝耗(已由 CombatSystem 校验扣除)。
struct CastContext {
    beast::platform::engine::context::EngineContext* ctx{nullptr};
    WorldState* world{nullptr};
    CombatSystem* combat{nullptr};   // 供 ISkill 调 apply_damage / spawn projectile
    beast::platform::Tick tick{0};

    beast::platform::EntityId caster_eid{0};
    std::uint32_t skill_id{0};
    std::uint32_t skill_level{0};
    Vec2f target_pos{};
    beast::platform::EntityId target_eid{0};

    // 已由 CombatSystem 查好的配表行(可能为 nullptr,技能逻辑自行判空)
    const beast::biz::moba::pixel_moba::skill::SkillRowServer* skill_row{nullptr};
    const beast::biz::moba::pixel_moba::skill_level::SkillLevelRowServer* level_row{nullptr};
};

// 技能逻辑钩子。每个技能 ID 对应一个 ISkill 实现,在 CombatSystem::on_start 注册。
// cast() 内可:调用 combat->apply_damage、world->add_buff、spawn 飞行物、广播 Notify 等。
// 实现应保持幂等安全:同一 tick 内被调一次,不做 CD/蓝耗校验(已由 CombatSystem 完成)。
class ISkill {
public:
    virtual ~ISkill() = default;
    virtual void cast(CastContext& cx) = 0;
};

// 技能注册表:skill_id → ISkill。CombatSystem 持有,on_start 注册内置技能,
// 玩法扩展可在 on_start 后追加自定义技能(register_skill)。
class SkillRegistry {
public:
    void register_skill(std::uint32_t skill_id, std::unique_ptr<ISkill> skill) {
        skills_[skill_id] = std::move(skill);
    }

    [[nodiscard]] ISkill* find(std::uint32_t skill_id) const {
        const auto it = skills_.find(skill_id);
        return it == skills_.end() ? nullptr : it->second.get();
    }

private:
    std::unordered_map<std::uint32_t, std::unique_ptr<ISkill>> skills_;
};

} // namespace beast::moba::pixel
