#pragma once

#include <cstdint>
#include <mutex>

namespace beast::platform::core {

// 64-bit snowflake: 41-bit timestamp + 10-bit machine id + 12-bit sequence.
class IdGenerator {
public:
    static IdGenerator& instance(std::uint16_t machine_id = 0);

    [[nodiscard]] std::uint64_t next_id();

    void set_machine_id(std::uint16_t machine_id);

private:
    explicit IdGenerator(std::uint16_t machine_id);

    IdGenerator(const IdGenerator&) = delete;
    IdGenerator& operator=(const IdGenerator&) = delete;

    [[nodiscard]] std::uint64_t wait_next_millis(std::uint64_t last_timestamp);

    std::mutex mutex_;
    std::uint16_t machine_id_;
    std::uint64_t sequence_;
    std::uint64_t last_timestamp_;

    static constexpr std::uint64_t kEpoch = 1'700'000'000'000ULL;
    static constexpr std::uint8_t kMachineIdBits = 10;
    static constexpr std::uint8_t kSequenceBits = 12;
    static constexpr std::uint64_t kMaxMachineId = (1ULL << kMachineIdBits) - 1;
    static constexpr std::uint64_t kMaxSequence = (1ULL << kSequenceBits) - 1;
    static constexpr std::uint8_t kMachineIdShift = kSequenceBits;
    static constexpr std::uint8_t kTimestampShift = kSequenceBits + kMachineIdBits;
};

} // namespace beast::platform::core
