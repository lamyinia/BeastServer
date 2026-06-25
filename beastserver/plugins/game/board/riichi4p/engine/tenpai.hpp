#pragma once

#include <string>
#include <vector>

namespace beast::board::riichi4p {

// 13 张牌是否听牌（标准 4 面子 + 1 雀头，含数牌顺子/刻子）。
[[nodiscard]] bool is_tenpai(const std::vector<std::string>& tiles_13);

// 14 张牌打掉 discard 后是否听牌（立直宣言打该牌的前置条件）。
[[nodiscard]] bool is_tenpai_after_discard(
    const std::vector<std::string>& tiles_14,
    const std::string& discard);

} // namespace beast::board::riichi4p
