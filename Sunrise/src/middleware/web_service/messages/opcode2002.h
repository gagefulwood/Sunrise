#pragma once

#include "../../encoding/byte_order.h"
#include "../web_service_envelope.h"

namespace sunrise::middleware::web_service::messages::opcode2002 {

/** Native item/vendor action opcode; not every source identity denotes an engram. */
inline constexpr std::uint16_t kOpcode = 2002;
/** Supported owned-engram requests contain a qword identity and one zero trailer byte. */
inline constexpr std::size_t kRequestBytes = encoding::kU64Size + 1;

/**
 * Accepts only the supported owned-item request shape.
 * @param message Parsed web-service envelope.
 * @param instanceSoid Receives the source identity; zero on failure.
 * @return False for other opcodes, lengths, trailers or a null identity.
 */
[[nodiscard]] inline bool parse_request(const Message& message,
                                        std::uint64_t& instanceSoid) noexcept {
    instanceSoid = 0;
    if (message.opcode != kOpcode || message.payload.size() != kRequestBytes
        || message.payload.back() != std::byte{}) {
        return false;
    }
    instanceSoid = encoding::read_u64_be(message.payload.first<encoding::kU64Size>());
    return instanceSoid != 0;
}

} // namespace sunrise::middleware::web_service::messages::opcode2002
