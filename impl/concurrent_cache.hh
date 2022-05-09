#ifndef CONCURRENT_CACHE_HEADER_ONLY_
#define CONCURRENT_CACHE_HEADER_ONLY_

#include <list>
#include <memory>
#include <shared_mutex>
#include <unordered_map>

namespace lib {

    template <typename K, typename V>
    class lru_cache {
    public:
        using key_type = K;
        using value_type = V;

        using capacity_type = size_t;
        using lru_iterator = typename std::list<key_type>::iterator;

        lru_cache(const capacity_type& capacity = 64);
        ~lru_cache() = default;

        // Insert value by copy
        void insert(const key_type& key, const value_type& value);
        // Insert value by move
        void insert(const key_type& key, value_type&& value);

        std::shared_ptr<value_type> get(const key_type& key);

        // Returns current cache size
        capacity_type size();
        // Returns maximum cache size
        capacity_type capacity();

        void clear();

    private:
        void touch(const key_type& key);

        const capacity_type capacity_ = 64;
        std::shared_mutex mutex_ {};
        
        std::unordered_map<key_type, std::shared_ptr<value_type>> cache_ {};

        std::list<key_type> lru_queue_ {};
        std::unordered_map<key_type, lru_iterator> keys_ {};
    };

    template <typename K, typename V>
    inline lru_cache<K, V>::lru_cache(const capacity_type& capacity) 
        : capacity_(capacity == 0 ? 64 : capacity)
    {}

    template <typename K, typename V>
    inline void lru_cache<K, V>::insert(const key_type& key, const value_type& value) {
        std::unique_lock<std::shared_mutex> lock_{ mutex_ };

        if (cache_.find(key) == cache_.end()) {
            if (cache_.size() + 1 > capacity_) {
                cache_.erase(lru_queue_.back());
                lru_queue_.pop_back();
            }

            cache_[key] = std::make_shared<value_type>(value);
            lru_queue_.emplace_front(key);
            keys_[key] = lru_queue_.begin();

            return;
        }

        this->touch(key);
        cache_[key] = std::make_shared<value_type>(value);
    }

    template <typename K, typename V>
    inline void lru_cache<K, V>::insert(const key_type& key, value_type&& value) {
        std::unique_lock<std::shared_mutex> lock_ { mutex_ };

        if (cache_.find(key) == cache_.end()) {
            if (cache_.size() + 1 > capacity_) {
                cache_.erase(lru_queue_.back());
                lru_queue_.pop_back();
            }

            cache_[key] = std::make_shared<value_type>(std::move(value));
            lru_queue_.emplace_front(key);
            keys_[key] = lru_queue_.begin();

            return;
        }

        this->touch(key);
        cache_[key] = std::make_shared<value_type>(std::move(value));
    }

    template <typename K, typename V>
    inline std::shared_ptr<typename lru_cache<K, V>::value_type> lru_cache<K, V>::get(const key_type& key) {
        std::shared_lock<std::shared_mutex> lock_ { mutex_ };

        auto it = cache_.find(key);
        if (it == cache_.end()) {
            return {};
        }

        this->touch(key);
        return it->second;
    }

    template <typename K, typename V>
    inline typename lru_cache<K, V>::capacity_type lru_cache<K, V>::size() {
        std::shared_lock<std::shared_mutex> lock_ { mutex_ };
        return cache_.size();
    }

    template <typename K, typename V>
    inline typename lru_cache<K, V>::capacity_type lru_cache<K, V>::capacity() {
        return capacity_;
    }

    template <typename K, typename V>
    inline void lru_cache<K, V>::clear() {
        std::unique_lock<std::shared_mutex> lock_ { mutex_ };

        cache_.clear();
        lru_queue_.clear();
        keys_.clear();
    }

    template <typename K, typename V>
    inline void lru_cache<K, V>::touch(const key_type& key) {
        lru_queue_.splice(lru_queue_.begin(), lru_queue_, keys_[key]);
    }
}

#endif