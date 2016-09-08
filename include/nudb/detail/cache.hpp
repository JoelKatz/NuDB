//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_DETAIL_CACHE_HPP
#define NUDB_DETAIL_CACHE_HPP

#include <nudb/detail/arena.hpp>
#include <nudb/detail/bucket.hpp>
#include <nudb/detail/format.hpp>
#include <boost/iterator/transform_iterator.hpp>
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>
#include <unordered_map>

namespace nudb {
namespace detail {

// Associative container storing
// bucket blobs keyed by bucket index.
//
template<class = void>
class cache_t
{
public:
    using value_type = std::pair<nbuck_t, bucket>;

private:
    using map_type =
        std::unordered_map<nbuck_t, void*>;

    struct transform
    {
        using argument_type =
            typename map_type::value_type;
        using result_type = value_type;

        cache_t* cache_;

        transform()
            : cache_(nullptr)
        {
        }

        explicit
        transform(cache_t& cache)
            : cache_(&cache)
        {
        }

        value_type
        operator()(argument_type const& e) const
        {
            return std::make_pair(e.first,
                bucket{cache_->block_size_, e.second});
        }
    };

    nsize_t key_size_;
    nsize_t block_size_;
    arena arena_;
    map_type map_;

public:
    using iterator = boost::transform_iterator<
        transform, typename map_type::iterator,
            value_type, value_type>;

    cache_t(cache_t const&) = delete;
    cache_t& operator=(cache_t&&) = delete;
    cache_t& operator=(cache_t const&) = delete;

    cache_t();
    cache_t(cache_t&& other);

    explicit
    cache_t(nsize_t key_size, nsize_t block_size);

    std::size_t
    size() const
    {
        return map_.size();
    }

    iterator
    begin()
    {
        return iterator{map_.begin(), transform{*this}};
    }

    iterator
    end()
    {
        return iterator{map_.end(), transform{*this}};
    }

    bool
    empty() const
    {
        return map_.empty();
    }

    void
    clear();

    void
    shrink_to_fit();

    iterator
    find(nbuck_t n);

    // Create an empty bucket
    //
    bucket
    create(nbuck_t n);

    // Insert a copy of a bucket.
    //
    iterator
    insert(nbuck_t n, bucket const& b);

    template<class U>
    friend
    void
    swap(cache_t<U>& lhs, cache_t<U>& rhs);
};

// Constructs a cache that will never have inserts
template<class _>
cache_t<_>::
cache_t()
    : key_size_(0)
    , block_size_(0)
{
}

template<class _>
cache_t<_>::
cache_t(cache_t&& other)
    : key_size_{other.key_size_}
    , block_size_(other.block_size_)
    , arena_(std::move(other.arena_))
    , map_(std::move(other.map_))
{
}

template<class _>
cache_t<_>::
cache_t(nsize_t key_size, nsize_t block_size)
    : key_size_(key_size)
    , block_size_(block_size)
{
}

template<class _>
void
cache_t<_>::
clear()
{
    arena_.clear();
    map_.clear();
}

template<class _>
void
cache_t<_>::
shrink_to_fit()
{
    arena_.shrink_to_fit();
}

template<class _>
auto
cache_t<_>::
find(nbuck_t n) ->
    iterator
{
    auto const iter = map_.find(n);
    if(iter == map_.end())
        return iterator(map_.end(), transform(*this));
    return iterator(iter, transform(*this));
}

template<class _>
bucket
cache_t<_>::
create(nbuck_t n)
{
    auto const p = arena_.alloc(block_size_);
    map_.emplace(n, p);
    return bucket(block_size_, p, detail::empty);
}

template<class _>
auto
cache_t<_>::
insert(nbuck_t n, bucket const& b) ->
    iterator
{
    void* const p = arena_.alloc(b.block_size());
    ostream os(p, b.block_size());
    b.write(os);
    auto const result = map_.emplace(n, p);
    return iterator(result.first, transform(*this));
}

template<class U>
void
swap(cache_t<U>& lhs, cache_t<U>& rhs)
{
    using std::swap;
    swap(lhs.key_size_, rhs.key_size_);
    swap(lhs.block_size_, rhs.block_size_);
    swap(lhs.arena_, rhs.arena_);
    swap(lhs.map_, rhs.map_);
}

using cache = cache_t<>;

} // detail
} // nudb

#endif
