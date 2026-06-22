#include "beast/platform/core/id_generator.hpp"

#include "beast/platform/core/time.hpp"

#include <stdexcept>

namespace beast::platform::core {

IdGenerator& IdGenerator::instance(std::uint16_t machine_id) {
    static IdGenerator inst(machine_id);
    return inst;
}

IdGenerator::IdGenerator(std::uint16_t machine_id)
    : machine_id_(machine_id), sequence_(0), last_timestamp_(0) {
    if (machine_id > kMaxMachineId) {
        throw std::invalid_argument("machine id must be between 0 and 1023");
    }
}

void IdGenerator::set_machine_id(std::uint16_t machine_id) {
    std::lock_guard lock(mutex_);
    if (machine_id > kMaxMachineId) {
        throw std::invalid_argument("machine id must be between 0 and 1023");
    }
    machine_id_ = machine_id;
}

std::uint64_t IdGenerator::next_id() {
    std::lock_guard lock(mutex_);

    std::uint64_t timestamp = TimeUtil::now_millis();
    if (timestamp < last_timestamp_) {
        throw std::runtime_error("clock moved backwards");
    }

    if (timestamp == last_timestamp_) {
        sequence_ = (sequence_ + 1) & kMaxSequence;
        if (sequence_ == 0) {
            timestamp = wait_next_millis(last_timestamp_);
        }
    } else {
        sequence_ = 0;
    }

    last_timestamp_ = timestamp;
    return ((timestamp - kEpoch) << kTimestampShift)
         | (static_cast<std::uint64_t>(machine_id_) << kMachineIdShift)
         | sequence_;
}

std::uint64_t IdGenerator::wait_next_millis(std::uint64_t last_timestamp) {
    std::uint64_t timestamp = TimeUtil::now_millis();
    while (timestamp <= last_timestamp) {
        timestamp = TimeUtil::now_millis();
    }
    return timestamp;
}

} // namespace beast::platform::core
