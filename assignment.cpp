#include <algorithm>
#include <atomic>
#include <boost/thread/latch.hpp>
#include <thread>

#include "catch.hpp"

namespace detail {

struct ControlBlock {
public:
    using erased_deleter_t = std::function<void()>;
    explicit ControlBlock(erased_deleter_t deleter)
        : m_count{1}, m_deleter{std::move(deleter)} {}
    ControlBlock(const ControlBlock&) = delete;

    void delete_owned() { m_deleter(); }

    [[nodiscard]] std::size_t get_count() const noexcept { return m_count; }
    [[nodiscard]] std::size_t fetch_sub_1() noexcept {
        return m_count.fetch_sub(1);
    }
    void increment_count() noexcept { ++m_count; }
    void decrement_count() noexcept { ++m_count; }

private:
    std::atomic<std::size_t> m_count;
    const erased_deleter_t m_deleter;
};

template <typename F, typename T>
[[nodiscard]] auto make_deleter(F&& deleter, T* ptr) noexcept {
    return [deleter = std::forward<F>(deleter), ptr] { deleter(ptr); };
}
}  // namespace detail

template <typename T>
class SharedPtr {
public:
    using value_t = T;
    using ptr_t = T*;
    using reference_t = T&;
    using control_block_t = detail::ControlBlock;
    using deleter_t = std::function<void(std::remove_const_t<T>*)>;

    // TODO: limit that to base classes and const/non-const alternatives
    template <typename U>
    friend class SharedPtr;

    explicit SharedPtr() noexcept : m_control_block{nullptr}, m_ptr{nullptr} {}
    explicit SharedPtr(std::nullptr_t) noexcept
        : m_control_block{nullptr}, m_ptr{nullptr} {}

    explicit SharedPtr(
        ptr_t instance,
        deleter_t deleter = std::default_delete<std::remove_const_t<value_t>>{})
        : m_control_block{new detail::ControlBlock{
              detail::make_deleter(std::move(deleter), instance)}},
          m_ptr{instance} {}

    // TODO: handle class hierarchies
    explicit SharedPtr(const SharedPtr& other) noexcept
        : m_control_block{other.m_control_block}, m_ptr{other.m_ptr} {
        m_control_block->increment_count();
    }

    template <typename U>
    requires(std::is_const_v<T>and std::is_same_v<
             std::remove_const_t<T>, U>) explicit SharedPtr(const SharedPtr<U>&
                                                                other) noexcept
        : m_control_block{other.m_control_block}, m_ptr{other.m_ptr} {
        m_control_block->increment_count();
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

    void reset() noexcept {
        if (m_control_block) {
            const auto count_prior_to_decrement =
                m_control_block->fetch_sub_1();

            if (1 == count_prior_to_decrement) {
                m_control_block->delete_owned();
                // TODO: handle weak_ptr count
                delete m_control_block;
                m_control_block = nullptr;
                m_ptr = nullptr;
            }
        }
    }

    friend bool operator==(const SharedPtr& lhs,
                           const SharedPtr& rhs) noexcept {
        return lhs.get() == rhs.get();
    }

    SharedPtr& operator=(std::nullptr_t) noexcept {
        reset();
        return *this;
    }

    SharedPtr& operator=(ptr_t ptr) {
        if (get() != ptr) {
            // note: order below relevant for strong exception safety
            auto* new_control_block = new control_block_t{detail::make_deleter(
                std::default_delete<std::remove_const_t<value_t>>{}, ptr)};
            reset();
            m_control_block = new_control_block;
            m_ptr = ptr;
        }
        return *this;
    }

    SharedPtr& operator=(const SharedPtr& other) noexcept {
        if (*this != other) {
            static_assert(std::is_nothrow_copy_constructible_v<SharedPtr>);
            this->~SharedPtr();
            new (this) SharedPtr{other};
        }
        return *this;
    }

    ~SharedPtr() { reset(); }

    // TODO: re-consider rvalue specifiers
    ptr_t get() const noexcept { return m_ptr; }
    reference_t operator*() noexcept { return *get(); }
    [[nodiscard]] std::size_t use_count() const noexcept {
        return m_control_block ? m_control_block->get_count() : 0;
    }
    friend void swap(SharedPtr& lhs, SharedPtr& rhs) noexcept{
        using std::swap;
        swap(lhs.m_control_block, rhs.m_control_block);
        swap(lhs.m_ptr, rhs.m_ptr);
    }

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
        THEN("propagates the deleter on copy") {
            {
                SharedPtr<Traced> sut = [&] {
                    SharedPtr<Traced> sut2{new Traced{}, deleter};
                    SharedPtr<Traced> sut3{sut2};
                    return SharedPtr<Traced>{sut3};
                }();
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
    GIVEN("a shared pointer shared between threads") {
        SharedPtr<Traced> sut{new Traced{}};
        REQUIRE(sut.use_count() == 1);
        WHEN("copied and destructed asynchronously") {
            constexpr auto THREAD_COUNT = 100;

            boost::latch start_latch{THREAD_COUNT + 1};
            const auto construct_destruct_multiple_times = [&start_latch,
                                                            &sut] {
                // note: have all threads start from this point simultaneously
                // to increase competition
                start_latch.count_down_and_wait();
                for (int i = 0; i < 10000; ++i) {
                    SharedPtr<Traced> sut2{sut};
                }
            };

            THEN("will always handle resource properly") {
                std::vector<std::thread> threads{};
                std::generate_n(
                    std::back_insert_iterator{threads}, THREAD_COUNT, [&] {
                        return std::thread{construct_destruct_multiple_times};
                    });
                start_latch.count_down_and_wait();  // green light
                for (auto& th : threads) {
                    if (th.joinable()) {
                        th.join();
                    }
                }

                CHECK(sut.use_count() == 1);
                CHECK(Traced::alive_count() == 1);
            }
        }
    }
}

TEST_CASE("Assignment") {
    GIVEN("a shared pointer") {
        SharedPtr<Traced> sut{new Traced{}};
        REQUIRE(Traced::alive_count() == 1);
        WHEN("assigned nullptr") {
            sut = nullptr;
            THEN("it's value will be reset") {
                CHECK(sut.get() == nullptr);
                CHECK(Traced::alive_count() == 0);
            }
        }
        WHEN("assigned its value") {
            sut = sut.get();
            THEN("it's value will be proper") {
                CHECK(sut.get() != nullptr);
                CHECK(Traced::alive_count() == 1);
            }
        }
        WHEN("assigned itself") {
            sut = sut;
            THEN("it's value will be proper") {
                CHECK(sut.get() != nullptr);
                CHECK(Traced::alive_count() == 1);
            }
        }
    }
}

TEST_CASE("Swap")
{
    GIVEN("two shared ptr instances"){
        auto* ptr1 = new Traced{};
        auto* ptr2 = new Traced{};
        SharedPtr<Traced> sut1{ptr1};
        SharedPtr<Traced> sut2{ptr2};
        WHEN("swapped"){
            swap(sut1, sut2);
            THEN("will point to swapped memory locations"){
                CHECK(sut1.get() == ptr2);
                CHECK(sut2.get() == ptr1);
            }
            THEN("will have proper lifetime managed"){
                CHECK(Traced::alive_count() == 2);
                CHECK(sut1.use_count() == 1);
                CHECK(sut2.use_count() == 1);
            }
        }
    }
}
