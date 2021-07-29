#include <atomic>

#include "catch.hpp"

namespace detail {

struct ControlBlock {
    ControlBlock() : m_count{1} {}
    ControlBlock(const ControlBlock&) = delete;

    std::atomic<std::size_t> m_count;
};
}  // namespace detail

template <typename T>
class SharedPtr {
public:
    using ptr_t = T*;
    using reference_t = T&;

    explicit SharedPtr(ptr_t instance)
        : m_control_block{new detail::ControlBlock{}}, m_ptr{instance} {}

    // TODO: handle class hierarchies
    // TODO: handle const T
    explicit SharedPtr(const SharedPtr& other)
        : m_control_block{other.m_control_block}, m_ptr{other.m_ptr} {
        ++m_control_block->m_count;
    }

    //    explicit SharedPtr(SharedPtr&& other) noexcept
    //        : m_control_block{other.m_control_block}, m_ptr{other.m_ptr}
    //    {
    //
    //    }

    ~SharedPtr() {
        // TODO: handle erased deleter
        // TODO: handle atomic access
        --m_control_block->m_count;
        if (!m_control_block->m_count.load()) {
            delete m_ptr;
            delete m_control_block;
        }
    }

    ptr_t get() const noexcept { return m_ptr; }
    reference_t operator*() noexcept { return *get(); }

private:
    detail::ControlBlock* m_control_block;
    ptr_t m_ptr;
};

class Traced {
public:
    Traced() { ++g_count; }
    ~Traced() { --g_count; }

    static int alive_count() { return g_count; }

private:
    static inline std::atomic<int> g_count = 0;
};

/**
 * NOTE: the requirements text tree of this file can be roughly displayed by:
    grep -oE \
         "(\s)*(AND_GIVEN|GIVEN|AND_WHEN|WHEN|AND_THEN|THEN|SECTION|TEST_CASE)\(\".*\"\)"\
         tests/tests.cpp \
         | less
*/

TEST_CASE("Construction") {
    WHEN("constructed with a pointer") {
        Traced* value = new Traced{};
        THEN("takes ownership & deletes on destruction") {
            {
                SharedPtr<Traced> sut{value};
                CHECK(Traced::alive_count() == 1);
            }
            CHECK(Traced::alive_count() == 0);
        }
        THEN("holds the given pointer") {
            SharedPtr<Traced> sut{value};
            CHECK(sut.get() == value);
            CHECK(&*sut == value);
        }
    }
    WHEN("copy-constructed") {
        SharedPtr<Traced> sut1{new Traced{}};
        SharedPtr<Traced> sut2{sut1};
        THEN("holds the same pointer as original")
        {
            CHECK(sut1.get() == sut2.get());
        }
    }
}
