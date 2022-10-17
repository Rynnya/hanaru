#ifndef CONCURRENT_CACHE_HEADER_09052022_
#define CONCURRENT_CACHE_HEADER_09052022_

#include <list>
#include <memory>
#include <shared_mutex>
#include <unordered_map>

namespace cache {

    template <typename K, typename V, size_t SIZE = 256>
    class LRUCache {
    public:
        using KeyT = K;
        using ValueT = V;
        using SharedValueT = std::shared_ptr<const ValueT>;

        using CapacityT = size_t;
        using LRUIterator = typename std::list<KeyT>::iterator;

        LRUCache() = default;
        ~LRUCache() = default;

        SharedValueT insert(const KeyT& key, const ValueT& value);
        SharedValueT insert(const KeyT& key, ValueT&& value);
        SharedValueT insert(const KeyT& key, std::nullptr_t);

        SharedValueT find(const KeyT& key) const;

        // Returns current cache size
        CapacityT size() const noexcept;
        // Returns maximum cache size
        CapacityT capacity() const noexcept;

        SharedValueT pop();
        void clear();

    private:
        void touch(const KeyT& key) const;

        mutable std::shared_mutex mutex_ {};

        mutable std::list<KeyT> lruQueue_ {};
        std::unordered_map<KeyT, LRUIterator> keys_ {};
        std::unordered_map<KeyT, SharedValueT> cache_ {};
    };

    template <typename K, typename V, size_t SIZE>
    inline typename LRUCache<K, V, SIZE>::SharedValueT LRUCache<K, V, SIZE>::insert(const KeyT& key, const ValueT& value) {
        std::unique_lock<std::shared_mutex> lock { mutex_ };

        if (cache_.find(key) == cache_.end()) {
            if (cache_.size() + 1 > SIZE) {
                cache_.erase(lruQueue_.back());
                lruQueue_.pop_back();
            }

            auto result = (cache_[key] = std::make_shared<const ValueT>(value));
            lruQueue_.emplace_front(key);
            keys_[key] = lruQueue_.begin();

            return result;
        }

        this->touch(key);
        return (cache_[key] = std::make_shared<const ValueT>(value));
    }

    template <typename K, typename V, size_t SIZE>
    inline typename LRUCache<K, V, SIZE>::SharedValueT LRUCache<K, V, SIZE>::insert(const KeyT& key, ValueT&& value) {
        std::unique_lock<std::shared_mutex> lock { mutex_ };

        if (cache_.find(key) == cache_.end()) {
            if (cache_.size() + 1 > SIZE) {
                cache_.erase(lruQueue_.back());
                lruQueue_.pop_back();
            }

            auto result = (cache_[key] = std::make_shared<const ValueT>(std::move(value)));
            lruQueue_.emplace_front(key);
            keys_[key] = lruQueue_.begin();

            return result;
        }

        this->touch(key);
        return (cache_[key] = std::make_shared<const ValueT>(std::move(value)));
    }

    template <typename K, typename V, size_t SIZE>
    inline typename LRUCache<K, V, SIZE>::SharedValueT LRUCache<K, V, SIZE>::insert(const KeyT& key, std::nullptr_t) {
        std::unique_lock<std::shared_mutex> lock { mutex_ };

        if (cache_.find(key) == cache_.end()) {
            if (cache_.size() + 1 > SIZE) {
                cache_.erase(lruQueue_.back());
                lruQueue_.pop_back();
            }

            auto result = (cache_[key] = std::shared_ptr<const ValueT>());
            lruQueue_.emplace_front(key);
            keys_[key] = lruQueue_.begin();

            return result;
        }

        this->touch(key);
        return (cache_[key] = std::shared_ptr<const ValueT>());
    }

    template <typename K, typename V, size_t SIZE>
    inline typename LRUCache<K, V, SIZE>::SharedValueT LRUCache<K, V, SIZE>::find(const KeyT& key) const {
        std::shared_lock<std::shared_mutex> lock { mutex_ };

        auto it = cache_.find(key);
        if (it == cache_.end()) {
            return nullptr;
        }

        this->touch(key);
        return it->second;
    }

    template <typename K, typename V, size_t SIZE>
    inline typename LRUCache<K, V, SIZE>::CapacityT LRUCache<K, V, SIZE>::size() const noexcept {
        std::shared_lock<std::shared_mutex> lock { mutex_ };
        return cache_.size();
    }

    template <typename K, typename V, size_t SIZE>
    inline typename LRUCache<K, V, SIZE>::CapacityT LRUCache<K, V, SIZE>::capacity() const noexcept {
        return SIZE;
    }

    template <typename K, typename V, size_t SIZE>
    inline typename LRUCache<K, V, SIZE>::SharedValueT LRUCache<K, V, SIZE>::pop() {
        std::unique_lock<std::shared_mutex> lock { mutex_ };

        if (cache_.size() == 0) {
            return nullptr;
        }
        
        auto it = cache_.extract(lruQueue_.back());
        lruQueue_.pop_back();
        return it.mapped();
    }

    template <typename K, typename V, size_t SIZE>
    inline void LRUCache<K, V, SIZE>::clear() {
        std::unique_lock<std::shared_mutex> lock { mutex_ };

        lruQueue_.clear();
        keys_.clear();
        cache_.clear();
    }

    template <typename K, typename V, size_t SIZE>
    inline void LRUCache<K, V, SIZE>::touch(const KeyT& key) const {
        lruQueue_.splice(lruQueue_.begin(), lruQueue_, keys_.at(key));
    }

}

#endif