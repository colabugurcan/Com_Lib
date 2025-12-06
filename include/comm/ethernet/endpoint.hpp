/**
 * @file endpoint.hpp
 * @brief DO-178C Level B Compliant Network Endpoint Definition
 * 
 * This module provides the Endpoint structure for network address/port
 * configuration in flight-critical avionics systems.
 * 
 * @note Simple data structure with no dynamic behavior.
 * 
 * @requirement SRS-NET-001: Network endpoint definition
 * 
 * @copyright (C) 2025 - Flight Critical Systems
 * @version 2.0.0 - DO-178C Compliant
 */

#pragma once

#include <cstdint>
#include <string>

namespace comm::ethernet {

struct Endpoint {
	std::string address{};
	std::uint16_t port{0};
};

} // namespace comm::ethernet
