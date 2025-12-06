/**
 * @file do178c.hpp
 * @brief DO-178C Compliance Utilities for Flight-Critical Software
 * 
 * This header provides utilities and macros for DO-178C Level A/B/C compliance:
 * - Static assertions for compile-time verification
 * - Runtime assertion macros with configurable behavior
 * - Bounded loop constructs
 * - Memory safety utilities
 * - Traceability annotations
 * 
 * @note All utilities are designed for deterministic, exception-free operation.
 * 
 * DO-178C Compliance Areas:
 * - Table A-1: Software Planning Process
 * - Table A-2: Software Development Process  
 * - Table A-3: Verification of Outputs
 * - Table A-4: Software Testing Process
 * - Table A-5: Software Configuration Management
 * - Table A-6: Software Quality Assurance
 * - Table A-7: Certification Liaison Process
 * 
 * @requirement SRS-DO178C-001: Compliance framework
 * @requirement SRS-DO178C-002: Runtime assertions
 * @requirement SRS-DO178C-003: Bounded loop constructs
 * @requirement SRS-DO178C-004: Traceability annotations
 * 
 * @copyright (C) 2025 - Flight Critical Systems
 * @version 2.0.0 - DO-178C Compliant
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <limits>
#include <array>

// ============================================================================
// DO-178C SOFTWARE LEVEL CONFIGURATION
// ============================================================================

/**
 * @brief Software Design Assurance Level (DAL)
 * 
 * DO-178C defines 5 levels based on failure condition severity:
 * - Level A: Catastrophic (loss of aircraft)
 * - Level B: Hazardous/Severe-Major
 * - Level C: Major
 * - Level D: Minor  
 * - Level E: No Effect
 */
#ifndef COMM_DO178C_DAL
#define COMM_DO178C_DAL 'B'  // Default to Level B
#endif

// Enable strict checking for Level A and B
#if (COMM_DO178C_DAL == 'A') || (COMM_DO178C_DAL == 'B')
#define COMM_DO178C_STRICT 1
#else
#define COMM_DO178C_STRICT 0
#endif

// ============================================================================
// TRACEABILITY ANNOTATIONS
// ============================================================================

/**
 * @brief Requirement traceability macro
 * 
 * Use to link code to requirements documents.
 * @param req_id Requirement identifier (e.g., "SRS-COMM-001")
 */
#define COMM_REQUIREMENT(req_id) /* Requirement: req_id */

/**
 * @brief Test case traceability macro
 * 
 * Use to link code to test cases.
 * @param test_id Test case identifier (e.g., "TC-COMM-001")
 */
#define COMM_TESTCASE(test_id) /* TestCase: test_id */

/**
 * @brief Design decision documentation
 * @param decision Brief description of design decision and rationale
 */
#define COMM_DESIGN_DECISION(decision) /* Design: decision */

/**
 * @brief Safety-critical code marker
 * 
 * Marks functions/sections that are safety-critical and require
 * additional verification and testing coverage.
 */
#define COMM_SAFETY_CRITICAL /* Safety-Critical Code */

/**
 * @brief Dead code marker (intentionally unreachable)
 * 
 * Marks code that should never execute under normal conditions.
 * Used for defensive programming.
 */
#define COMM_UNREACHABLE() do { \
    COMM_ASSERT_ALWAYS(false, "Unreachable code executed"); \
} while(false)

// ============================================================================
// ASSERTION FRAMEWORK
// ============================================================================

namespace comm::do178c {

/**
 * @brief Assertion failure handler type
 * 
 * Called when an assertion fails. In production, this should
 * trigger appropriate failsafe behavior.
 */
using AssertionHandler = void(*)(const char* file, int line, const char* expr, const char* msg);

/// Global assertion handler (can be replaced for testing)
inline AssertionHandler g_assertionHandler = nullptr;

/**
 * @brief Set custom assertion handler
 * 
 * @param handler Function to call on assertion failure
 */
inline void setAssertionHandler(AssertionHandler handler) noexcept {
    g_assertionHandler = handler;
}

/**
 * @brief Default assertion failure behavior
 * 
 * In DO-178C systems, assertion failure typically triggers:
 * 1. Error logging
 * 2. Failsafe state transition
 * 3. System reset or safe shutdown
 */
[[noreturn]] inline void defaultAssertionFailed(
    const char* file, 
    int line, 
    const char* expr, 
    const char* msg) noexcept 
{
    // In production: log error, trigger failsafe, halt
    // For now: infinite loop (MCU watchdog will reset)
    (void)file;
    (void)line;
    (void)expr;
    (void)msg;
    
    // DO-178C requires deterministic failure behavior
    while (true) {
        // Spin - watchdog timer should reset system
        // This prevents undefined behavior on assertion failure
    }
}

/**
 * @brief Assertion check with custom message
 */
inline void assertCheck(
    bool condition, 
    const char* file, 
    int line, 
    const char* expr, 
    const char* msg = "") noexcept 
{
    if (!condition) {
        if (g_assertionHandler != nullptr) {
            g_assertionHandler(file, line, expr, msg);
        }
        defaultAssertionFailed(file, line, expr, msg);
    }
}

} // namespace comm::do178c

/**
 * @brief Runtime assertion macro (can be disabled for production)
 */
#if COMM_DO178C_STRICT || !defined(NDEBUG)
#define COMM_ASSERT(condition, msg) \
    ::comm::do178c::assertCheck((condition), __FILE__, __LINE__, #condition, (msg))
#else
#define COMM_ASSERT(condition, msg) ((void)0)
#endif

/**
 * @brief Always-enabled assertion (never disabled, even in release)
 * 
 * Use for critical safety checks that must always execute.
 */
#define COMM_ASSERT_ALWAYS(condition, msg) \
    ::comm::do178c::assertCheck((condition), __FILE__, __LINE__, #condition, (msg))

/**
 * @brief Precondition assertion
 */
#define COMM_PRECONDITION(condition) \
    COMM_ASSERT((condition), "Precondition violated")

/**
 * @brief Postcondition assertion  
 */
#define COMM_POSTCONDITION(condition) \
    COMM_ASSERT((condition), "Postcondition violated")

/**
 * @brief Invariant assertion
 */
#define COMM_INVARIANT(condition) \
    COMM_ASSERT((condition), "Invariant violated")

// ============================================================================
// BOUNDED LOOP CONSTRUCTS
// ============================================================================

namespace comm::do178c {

/**
 * @brief Maximum iterations constant for bounded loops
 * 
 * All loops in DO-178C code must have provable termination.
 * This constant provides a safe upper bound.
 */
inline constexpr std::size_t kMaxIterations = 10000U;

/**
 * @brief Bounded loop counter with overflow protection
 */
class BoundedCounter {
public:
    constexpr explicit BoundedCounter(std::size_t maxCount) noexcept
        : maxCount_(maxCount), count_(0) {}

    /**
     * @brief Increment counter and check bound
     * @return true if within bounds, false if limit reached
     */
    [[nodiscard]] bool increment() noexcept {
        if (count_ >= maxCount_) {
            return false;  // Limit reached
        }
        ++count_;
        return true;
    }

    /**
     * @brief Check if more iterations are allowed
     */
    [[nodiscard]] bool hasRemaining() const noexcept {
        return count_ < maxCount_;
    }

    /**
     * @brief Get current count
     */
    [[nodiscard]] std::size_t count() const noexcept {
        return count_;
    }

    /**
     * @brief Reset counter
     */
    void reset() noexcept {
        count_ = 0;
    }

private:
    const std::size_t maxCount_;
    std::size_t count_;
};

} // namespace comm::do178c

/**
 * @brief Bounded for loop macro
 * 
 * Ensures loop has provable termination with maximum iteration count.
 * 
 * @param var Loop variable name
 * @param start Starting value
 * @param end Ending value (exclusive)
 * @param max Maximum allowed iterations
 */
#define COMM_BOUNDED_FOR(var, start, end, max) \
    for (std::size_t var = (start), _bound_limit_ = 0; \
         (var) < (end) && _bound_limit_ < (max); \
         ++(var), ++_bound_limit_)

/**
 * @brief Bounded while loop macro
 * 
 * @param condition Loop condition
 * @param max Maximum allowed iterations
 */
#define COMM_BOUNDED_WHILE(condition, max) \
    for (std::size_t _bound_counter_ = 0; \
         (condition) && _bound_counter_ < (max); \
         ++_bound_counter_)

// ============================================================================
// STATIC ANALYSIS HELPERS
// ============================================================================

namespace comm::do178c {

/**
 * @brief Compile-time size validation
 */
template<typename T, std::size_t ExpectedSize>
struct SizeCheck {
    static_assert(sizeof(T) == ExpectedSize, "Type size mismatch");
    static constexpr bool value = (sizeof(T) == ExpectedSize);
};

/**
 * @brief Compile-time alignment validation
 */
template<typename T, std::size_t ExpectedAlign>
struct AlignCheck {
    static_assert(alignof(T) == ExpectedAlign, "Type alignment mismatch");
    static constexpr bool value = (alignof(T) == ExpectedAlign);
};

/**
 * @brief Ensure type is trivially copyable (safe for memcpy)
 */
template<typename T>
struct TriviallyCopyableCheck {
    static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable");
    static constexpr bool value = std::is_trivially_copyable_v<T>;
};

/**
 * @brief Ensure type is standard layout (safe for C interop)
 */
template<typename T>
struct StandardLayoutCheck {
    static_assert(std::is_standard_layout_v<T>, "Type must be standard layout");
    static constexpr bool value = std::is_standard_layout_v<T>;
};

} // namespace comm::do178c

// ============================================================================
// SAFE ARITHMETIC OPERATIONS
// ============================================================================

namespace comm::do178c {

/**
 * @brief Safe addition with overflow check
 * 
 * @return Result if no overflow, std::nullopt on overflow
 */
template<typename T>
[[nodiscard]] constexpr std::enable_if_t<std::is_integral_v<T>, bool>
safeAdd(T a, T b, T& result) noexcept {
    if constexpr (std::is_signed_v<T>) {
        // Signed overflow check
        if ((b > 0) && (a > std::numeric_limits<T>::max() - b)) {
            return false;  // Overflow
        }
        if ((b < 0) && (a < std::numeric_limits<T>::min() - b)) {
            return false;  // Underflow
        }
    } else {
        // Unsigned overflow check
        if (a > std::numeric_limits<T>::max() - b) {
            return false;  // Overflow
        }
    }
    result = a + b;
    return true;
}

/**
 * @brief Safe subtraction with underflow check
 */
template<typename T>
[[nodiscard]] constexpr std::enable_if_t<std::is_integral_v<T>, bool>
safeSub(T a, T b, T& result) noexcept {
    if constexpr (std::is_signed_v<T>) {
        if ((b < 0) && (a > std::numeric_limits<T>::max() + b)) {
            return false;  // Overflow
        }
        if ((b > 0) && (a < std::numeric_limits<T>::min() + b)) {
            return false;  // Underflow
        }
    } else {
        if (a < b) {
            return false;  // Underflow
        }
    }
    result = a - b;
    return true;
}

/**
 * @brief Safe multiplication with overflow check
 */
template<typename T>
[[nodiscard]] constexpr std::enable_if_t<std::is_integral_v<T>, bool>
safeMul(T a, T b, T& result) noexcept {
    if (a == 0 || b == 0) {
        result = 0;
        return true;
    }
    
    if constexpr (std::is_signed_v<T>) {
        if ((a > 0 && b > 0) || (a < 0 && b < 0)) {
            // Result should be positive
            if (a > 0) {
                if (a > std::numeric_limits<T>::max() / b) return false;
            } else {
                if (b < std::numeric_limits<T>::max() / a) return false;
            }
        } else {
            // Result should be negative
            if (a < 0) {
                if (a < std::numeric_limits<T>::min() / b) return false;
            } else {
                if (b < std::numeric_limits<T>::min() / a) return false;
            }
        }
    } else {
        if (a > std::numeric_limits<T>::max() / b) {
            return false;  // Overflow
        }
    }
    result = a * b;
    return true;
}

/**
 * @brief Safe division with zero check
 */
template<typename T>
[[nodiscard]] constexpr std::enable_if_t<std::is_integral_v<T>, bool>
safeDiv(T a, T b, T& result) noexcept {
    if (b == 0) {
        return false;  // Division by zero
    }
    
    if constexpr (std::is_signed_v<T>) {
        // Check for INT_MIN / -1 overflow
        if (a == std::numeric_limits<T>::min() && b == static_cast<T>(-1)) {
            return false;
        }
    }
    
    result = a / b;
    return true;
}

} // namespace comm::do178c

// ============================================================================
// FIXED-SIZE BUFFER (NO DYNAMIC ALLOCATION)
// ============================================================================

namespace comm::do178c {

/**
 * @brief Fixed-size buffer for DO-178C compliant memory usage
 * 
 * This buffer has a compile-time fixed capacity and does not
 * use dynamic memory allocation, making it suitable for
 * flight-critical systems.
 * 
 * @tparam T Element type
 * @tparam Capacity Maximum number of elements
 */
template<typename T, std::size_t Capacity>
class FixedBuffer {
    static_assert(Capacity > 0, "Capacity must be positive");
    static_assert(std::is_trivially_copyable_v<T>, "Element must be trivially copyable");

public:
    using value_type = T;
    using size_type = std::size_t;
    using iterator = T*;
    using const_iterator = const T*;

    constexpr FixedBuffer() noexcept : size_(0), data_{} {}

    /**
     * @brief Get current size
     */
    [[nodiscard]] constexpr size_type size() const noexcept { return size_; }

    /**
     * @brief Get maximum capacity
     */
    [[nodiscard]] static constexpr size_type capacity() noexcept { return Capacity; }

    /**
     * @brief Check if empty
     */
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

    /**
     * @brief Check if full
     */
    [[nodiscard]] constexpr bool full() const noexcept { return size_ >= Capacity; }

    /**
     * @brief Get remaining space
     */
    [[nodiscard]] constexpr size_type remaining() const noexcept { 
        return Capacity - size_; 
    }

    /**
     * @brief Add element to end
     * @return true if added, false if full
     */
    bool push_back(const T& value) noexcept {
        if (full()) {
            return false;
        }
        data_[size_] = value;
        ++size_;
        return true;
    }

    /**
     * @brief Remove last element
     * @return true if removed, false if empty
     */
    bool pop_back() noexcept {
        if (empty()) {
            return false;
        }
        --size_;
        return true;
    }

    /**
     * @brief Access element with bounds checking
     * @return Pointer to element, or nullptr if out of bounds
     */
    [[nodiscard]] T* at(size_type index) noexcept {
        if (index >= size_) {
            return nullptr;
        }
        return &data_[index];
    }

    [[nodiscard]] const T* at(size_type index) const noexcept {
        if (index >= size_) {
            return nullptr;
        }
        return &data_[index];
    }

    /**
     * @brief Direct data access
     */
    [[nodiscard]] T* data() noexcept { return data_.data(); }
    [[nodiscard]] const T* data() const noexcept { return data_.data(); }

    /**
     * @brief Iterator access
     */
    [[nodiscard]] iterator begin() noexcept { return data_.data(); }
    [[nodiscard]] iterator end() noexcept { return data_.data() + size_; }
    [[nodiscard]] const_iterator begin() const noexcept { return data_.data(); }
    [[nodiscard]] const_iterator end() const noexcept { return data_.data() + size_; }

    /**
     * @brief Clear buffer
     */
    void clear() noexcept { size_ = 0; }

    /**
     * @brief Resize buffer
     * @return true if successful, false if new size exceeds capacity
     */
    bool resize(size_type newSize) noexcept {
        if (newSize > Capacity) {
            return false;
        }
        size_ = newSize;
        return true;
    }

    /**
     * @brief Copy data from source
     * @return Number of elements copied
     */
    size_type assign(const T* src, size_type count) noexcept {
        if (src == nullptr) {
            return 0;
        }
        const size_type toCopy = (count > Capacity) ? Capacity : count;
        for (size_type i = 0; i < toCopy; ++i) {
            data_[i] = src[i];
        }
        size_ = toCopy;
        return toCopy;
    }

private:
    size_type size_;
    std::array<T, Capacity> data_;
};

} // namespace comm::do178c

// ============================================================================
// NULL POINTER SAFETY
// ============================================================================

namespace comm::do178c {

/**
 * @brief Safe pointer wrapper with null checking
 */
template<typename T>
class NotNull {
public:
    NotNull() = delete;  // Cannot be null
    NotNull(std::nullptr_t) = delete;  // Cannot be null

    constexpr explicit NotNull(T* ptr) noexcept : ptr_(ptr) {
        COMM_ASSERT_ALWAYS(ptr != nullptr, "NotNull constructed with nullptr");
    }

    [[nodiscard]] constexpr T* get() const noexcept { return ptr_; }
    [[nodiscard]] constexpr T& operator*() const noexcept { return *ptr_; }
    [[nodiscard]] constexpr T* operator->() const noexcept { return ptr_; }

    constexpr operator T*() const noexcept { return ptr_; }

private:
    T* ptr_;
};

/**
 * @brief Safe pointer dereference with null check
 * 
 * @return Pointer if not null, triggers assertion otherwise
 */
template<typename T>
[[nodiscard]] inline T* safeDeref(T* ptr, const char* context = "") noexcept {
    COMM_ASSERT_ALWAYS(ptr != nullptr, context);
    return ptr;
}

} // namespace comm::do178c

// ============================================================================
// DEFENSIVE COPY
// ============================================================================

namespace comm::do178c {

/**
 * @brief Safe memory copy with bounds checking
 * 
 * @param dest Destination buffer
 * @param destSize Destination buffer size
 * @param src Source buffer
 * @param count Bytes to copy
 * @return Number of bytes actually copied
 */
inline std::size_t safeCopy(
    void* dest, 
    std::size_t destSize,
    const void* src, 
    std::size_t count) noexcept 
{
    if (dest == nullptr || src == nullptr) {
        return 0;
    }
    
    const std::size_t toCopy = (count > destSize) ? destSize : count;
    
    // Use byte-by-byte copy for safety (no undefined behavior)
    auto* d = static_cast<std::uint8_t*>(dest);
    const auto* s = static_cast<const std::uint8_t*>(src);
    
    COMM_BOUNDED_FOR(i, 0, toCopy, destSize) {
        d[i] = s[i];
    }
    
    return toCopy;
}

/**
 * @brief Safe memory set with bounds checking
 */
inline std::size_t safeSet(
    void* dest,
    std::size_t destSize,
    std::uint8_t value,
    std::size_t count) noexcept
{
    if (dest == nullptr) {
        return 0;
    }
    
    const std::size_t toSet = (count > destSize) ? destSize : count;
    auto* d = static_cast<std::uint8_t*>(dest);
    
    COMM_BOUNDED_FOR(i, 0, toSet, destSize) {
        d[i] = value;
    }
    
    return toSet;
}

} // namespace comm::do178c

// ============================================================================
// ENUM VALIDATION
// ============================================================================

namespace comm::do178c {

/**
 * @brief Check if enum value is within valid range
 * 
 * @tparam E Enum type
 * @param value Value to check
 * @param minVal Minimum valid value
 * @param maxVal Maximum valid value
 */
template<typename E>
[[nodiscard]] constexpr bool isValidEnum(
    E value, 
    E minVal, 
    E maxVal) noexcept 
{
    using U = std::underlying_type_t<E>;
    return static_cast<U>(value) >= static_cast<U>(minVal) &&
           static_cast<U>(value) <= static_cast<U>(maxVal);
}

} // namespace comm::do178c

// ============================================================================
// CODE COVERAGE MARKERS
// ============================================================================

/**
 * @brief Mark branch for MC/DC coverage tracking
 * 
 * DO-178C Level A requires Modified Condition/Decision Coverage.
 */
#define COMM_BRANCH(id) /* Branch: id */

/**
 * @brief Mark decision point for coverage
 */
#define COMM_DECISION(id) /* Decision: id */

// ============================================================================
// COMPILE-TIME CONFIGURATION VALIDATION
// ============================================================================

namespace comm::do178c {

// Validate critical constants at compile time
static_assert(kMaxIterations > 0, "kMaxIterations must be positive");
static_assert(kMaxIterations <= 1000000U, "kMaxIterations too large");

} // namespace comm::do178c

#endif // COMM_CORE_DO178C_HPP - Note: This is intentionally not here as we use #pragma once
