//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_DETAIL_ARENA_HPP
#define NUDB_DETAIL_ARENA_HPP

#include <nudb/detail/mutex.hpp>
#include <boost/assert.hpp>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>

#ifndef NUDB_DEBUG_LOG
#define NUDB_DEBUG_LOG 0
#endif
#if NUDB_DEBUG_LOG
#include <beast/unit_test/dstream.hpp>
#endif

namespace nudb {
namespace detail {

/*  Custom memory manager that allocates in large blocks.

    No limit is placed on the size of an allocation but
    allocSize should be tuned upon construction to be a
    significant multiple of the average allocation size.

    When the arena is cleared, allocated memory is placed
    on a free list for re-use, avoiding future system calls.
*/
template<class = void>
class arena_t
{
    using clock_type = std::chrono::steady_clock;
    using time_point = typename clock_type::time_point;

    //using clock_type = 
    class element;

    std::size_t allocSize_ = 0;
    std::size_t nalloc_ = 0;
    element* used_ = nullptr;
    element* free_ = nullptr;
    time_point when_ = clock_type::now();

public:
    arena_t(arena_t const&) = delete;
    arena_t& operator=(arena_t&&) = delete;
    arena_t& operator=(arena_t const&) = delete;

    ~arena_t();

    arena_t() = default;

    arena_t(arena_t&& other);

    // Makes used blocks reusable
    void
    clear();

    // Deletes free blocks
    void
    shrink_to_fit();

    // Called every so often
    void
    periodic_activity(
        detail::unique_lock_type& m);

    std::uint8_t*
    alloc(std::size_t n);

    template<class U>
    friend
    void
    swap(arena_t<U>& lhs, arena_t<U>& rhs);

private:
    void
    dealloc(element*& list);
};

//------------------------------------------------------------------------------

template<class _>
class arena_t<_>::element
{
    std::size_t const capacity_;
    std::size_t used_ = 0;

public:
    element* next = nullptr;

    explicit
    element(std::size_t allocSize)
        : capacity_(allocSize)
    {
    }

    void
    clear()
    {
        used_ = 0;
    }

    std::size_t
    remain() const
    {
        return capacity_ - used_;
    }

    std::size_t
    capacity() const
    {
        return capacity_;
    }

    std::uint8_t*
    alloc(std::size_t n);
};

template<class _>
std::uint8_t*
arena_t<_>::element::
alloc(std::size_t n)
{
    if(n > capacity_ - used_)
        return nullptr;
    auto const p = const_cast<std::uint8_t*>(
        reinterpret_cast<uint8_t const*>(this + 1)
            ) + used_;
    used_ += n;
    return p;
}

//------------------------------------------------------------------------------

template<class _>
arena_t<_>::
~arena_t()
{
    dealloc(used_);
    dealloc(free_);
}

template<class _>
arena_t<_>::
arena_t(arena_t&& other)
    : allocSize_(other.allocSize_)
    , nalloc_(other.nalloc_)
    , used_(other.used_)
    , free_(other.free_)
    , when_(other.when_)
{
    other.nalloc_ = 0;
    other.used_ = nullptr;
    other.free_ = nullptr;
    other.when_ = clock_type::now();
    other.allocSize_ = 0;
}

template<class _>
void
arena_t<_>::
clear()
{
    while(used_)
    {
        auto const e = used_;
        used_ = used_->next;
        e->clear();
        if(e->remain() == allocSize_)
        {
            e->next = free_;
            free_ = e;
        }
        else
        {
            e->~element();
            delete[] reinterpret_cast<std::uint8_t*>(e);
        }
    }
}

template<class _>
void
arena_t<_>::
shrink_to_fit()
{
    dealloc(free_);
#if NUDB_DEBUG_LOG
    auto const size =
        [](element* e)
        {
            std::size_t n = 0;
            while(e)
            {
                ++n;
                e = e->next;
            }
            return n;
        };
    beast::unit_test::dstream dout{std::cout};
    dout << "shrink_to_fit: "
        "alloc=" << allocSize_ <<
        ", nalloc=" << nalloc_ <<
        ", used=" << size(used_) <<
        "\n";
#endif
}

template<class _>
void
arena_t<_>::
periodic_activity(
    detail::unique_lock_type& m)
{
    using namespace std::chrono;
    auto const now = clock_type::now();
    auto const elapsed = now - when_;
    if(elapsed < seconds{1})
        return;
    when_ = now;
    if(! m.owns_lock())
        m.lock();
    auto const rate = static_cast<std::size_t>(std::ceil(
        nalloc_ / duration_cast<duration<float>>(elapsed).count()));
#if NUDB_DEBUG_LOG
    beast::unit_test::dstream dout{std::cout};
#endif
    auto const size =
        [](element* e)
        {
            std::size_t n = 0;
            while(e)
            {
                ++n;
                e = e->next;
            }
            return n;
        };
    if(rate >= allocSize_ * 2)
    {
        // adjust up
        allocSize_ = std::max(rate, allocSize_ * 2);
        dealloc(free_);
#if NUDB_DEBUG_LOG
    dout <<
        "rate=" << rate <<
        ", alloc=" << allocSize_ << " UP"
        ", nalloc=" << nalloc_ <<
        ", used=" << size(used_) <<
        ", free=" << size(free_) <<
        "\n";
#endif
    }
    else if(rate <= allocSize_ / 2)
    {
        // adjust down
        allocSize_ /= 2;
        dealloc(free_);
#if NUDB_DEBUG_LOG
    dout <<
        "rate=" << rate <<
        ", alloc=" << allocSize_ << " DOWN"
        ", nalloc=" << nalloc_ <<
        ", used=" << size(used_) <<
        ", free=" << size(free_) <<
        "\n";
#endif
    }
    else
    {
#if NUDB_DEBUG_LOG
    dout <<
        "rate=" << rate <<
        ", alloc=" << allocSize_ <<
        ", nalloc=" << nalloc_ <<
        ", used=" << size(used_) <<
        ", free=" << size(free_) <<
        "\n";
#endif
    }
    nalloc_ = 0;
}

template<class _>
std::uint8_t*
arena_t<_>::
alloc(std::size_t n)
{
    // Undefined behavior: Zero byte allocations
    BOOST_ASSERT(n != 0);
    n = 8 *((n + 7) / 8);
    if(used_ && used_->remain() >= n)
    {
        nalloc_ += n;
        return used_->alloc(n);
    }
    if(free_ && free_->remain() >= n)
    {
        auto const e = free_;
        free_ = free_->next;
        e->next = used_;
        used_ = e;
        nalloc_ += n;
        return used_->alloc(n);
    }
    auto const size = std::max(allocSize_, n);
    auto const e = reinterpret_cast<element*>(
        new std::uint8_t[sizeof(element) + size]);
    ::new(e) element{size};
    e->next = used_;
    used_ = e;
    nalloc_ += n;
    return used_->alloc(n);
}

template<class _>
void
swap(arena_t<_>& lhs, arena_t<_>& rhs)
{
    using std::swap;
    swap(lhs.nalloc_, rhs.nalloc_);
    swap(lhs.used_, rhs.used_);
    // don't swap allocSize_, free_, or when_
}

template<class _>
void
arena_t<_>::dealloc(element*& list)
{
    while(list)
    {
        auto const e = list;
        list = list->next;
        e->~element();
        delete[] reinterpret_cast<std::uint8_t*>(e);
    }
}

using arena = arena_t<>;

} // detail
} // nudb

#endif
