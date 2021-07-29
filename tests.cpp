#include <atomic>

#include "catch.hpp"

namespace detail {

template <typename T>
struct ControlBlock {
public:
    using ptr_t = T*;
    using deleter_t = std::function<void(ptr_t)>;
    explicit ControlBlock(deleter_t deleter)
        : m_count{1}, m_deleter{std::move(deleter)} {}
    ControlBlock(const ControlBlock&) = delete;

    std::atomic<std::size_t> m_count;
    void delete_owned(const ptr_t ptr) { m_deleter(ptr); }

private:
    const deleter_t m_deleter;
};
}  // namespace detail

template <typename T>
class SharedPtr {
public:
    using value_t = T;
    using ptr_t = T*;
    using reference_t = T&;
    using control_block_t = detail::ControlBlock<std::remove_const_t<T>>;
    using deleter_t = typename control_block_t::deleter_t;

    // TODO: limit that to base classes and const/non-const alternatives
    template <typename U>
    friend class SharedPtr;

    explicit SharedPtr() noexcept : m_control_block{nullptr}, m_ptr{nullptr} {}
    explicit SharedPtr(std::nullptr_t) noexcept
        : m_control_block{nullptr}, m_ptr{nullptr} {}

    explicit SharedPtr(
        ptr_t instance,
        deleter_t deleter = std::default_delete<std::remove_const_t<value_t>>{})
        : m_control_block{new detail::ControlBlock<
              std::remove_const_t<value_t>>{std::move(deleter)}},
          m_ptr{instance} {}

    // TODO: handle class hierarchies
    explicit SharedPtr(const SharedPtr& other) noexcept
        : m_control_block{other.m_control_block}, m_ptr{other.m_ptr} {
        ++m_control_block->m_count;
    }

    template <typename U>
    requires(std::is_const_v<T>and std::is_same_v<
             std::remove_const_t<T>, U>) explicit SharedPtr(const SharedPtr<U>&
                                                                other) noexcept
        : m_control_block{other.m_control_block}, m_ptr{other.m_ptr} {
        ++m_control_block->m_count;
    }
    template <typename U>
    requires(std::is_const_v<T>and std::is_same_v<
             std::remove_const_t<T>, U>) explicit SharedPtr(SharedPtr<U>&&
                                                                other) noexcept
        : m_control_block{other.m_control_block}, m_ptr{other.m_ptr} {
        other.m_control_block = nullptr;
        other.m_ptr = nullptr;
    }

    explicit SharedPtr(SharedPtr&& other) noexcept
        : m_control_block{other.m_control_block}, m_ptr{other.m_ptr} {
        other.m_control_block = nullptr;
        other.m_ptr = nullptr;
    }

    ~SharedPtr() {
        // TODO: handle atomic access
        if (m_control_block) {
            --m_control_block->m_count;
            if (!m_control_block->m_count) {
                m_control_block->delete_owned(
                    const_cast<std::remove_const_t<value_t>*>(m_ptr));
                // TODO: handle weak_ptr count
                delete m_control_block;
            }
        }
    }

    // TODO: re-consider rvalue specifiers
    ptr_t get() const noexcept { return m_ptr; }
    reference_t operator*() noexcept { return *get(); }

private:
    control_block_t* m_control_block;
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
         tests.cpp \
         | less
*/

TEST_CASE("Construction") {
    WHEN("default-constructed") {
        SharedPtr<Traced> sut{};
        THEN("will be null") { CHECK(sut.get() == nullptr); }
    }
    WHEN("constructed with nullptr") {
        SharedPtr<Traced> sut{nullptr};
        THEN("will be null") { CHECK(sut.get() == nullptr); }
    }
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
        THEN("holds the same pointer as original") {
            CHECK(sut1.get() == sut2.get());
        }
    }
    WHEN("move-constructed") {
        SharedPtr<Traced> sut1{new Traced{}};
        SharedPtr<Traced> sut2{std::move(sut1)};
        THEN("the original is nullptr") {
            CHECK(sut2.get());
            CHECK(sut1.get() == nullptr);
        }
    }
    WHEN("constructed with a pointer & deleter") {
        bool deleter_called = false;
        auto deleter = [&deleter_called](const Traced* ptr) {
            delete ptr;
            deleter_called = true;
        };
        THEN("destructs using the deleter") {
            {
                SharedPtr<Traced> sut{new Traced{}, deleter};
                CHECK(Traced::alive_count() == 1);
                CHECK(!deleter_called);
            }
            CHECK(Traced::alive_count() == 0);
            CHECK(deleter_called);
        }
    }
    GIVEN("a shared pointer to a mutable object") {
        SharedPtr<Traced> sut{new Traced{}};
        WHEN("copy-constructing a pointer to const") {
            SharedPtr<const Traced> sut2{sut};
            REQUIRE(Traced::alive_count() == 1);
            THEN("will point to the same instance") {
                CHECK(sut.get() == sut2.get());
            }
        }
        WHEN("move-constructing a pointer to const") {
            SharedPtr<const Traced> sut2{std::move(sut)};
            REQUIRE(Traced::alive_count() == 1);
            THEN("will take sole ownership") {
                CHECK(sut2.get());
                CHECK(not sut.get());
            }
        }
    }
    GIVEN("a shared pointer shared between instances") {
        REQUIRE(Traced::alive_count() == 0);
        std::vector pointers = []() -> std::vector<SharedPtr<Traced>> {
            SharedPtr<Traced> sut{new Traced{}};
            return std::vector{SharedPtr<Traced>{sut}, SharedPtr<Traced>{sut},
                               SharedPtr<Traced>{sut}};
        }();
        REQUIRE(Traced::alive_count() == 1);
        WHEN("one of the pointers is destructed") {
            pointers.pop_back();
            THEN("the object is still valid") {
                REQUIRE(Traced::alive_count() == 1);
                AND_WHEN("all the shared pointers are destructed") {
                    pointers.clear();
                    THEN("the object is destroyed") {
                        CHECK(Traced::alive_count() == 0);
                    }
                }
            }
        }
    }
}
