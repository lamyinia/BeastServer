#pragma once

#include <string>
#include <vector>

namespace beast::demo::stress_tick {

// 简化版听牌判断算法（仅支持标准 4 面子 + 1 雀头）。
//
// 支持的牌：
//   数牌：1m-9m / 1p-9p / 1s-9s
//   字牌：east / south / west / north（不含白发中，简化）
//
// 算法思路：
//   1. 把 13 张牌按 (suit, rank) 计数
//   2. 遍历所有可能的"听牌 X"，13+X=14 张
//   3. 14 张胡牌判断：取 1 雀头 + 4 面子（顺子/刻子）
//
// 输入：13 张牌字符串数组，如 ["1m","2m","3m","4p","5p","6p","7s","8s","9s","east","east","east","west"]
// 输出：是否听牌
[[nodiscard]] bool is_tenpai(const std::vector<std::string>& tiles_13);

} // namespace beast::demo::stress_tick
