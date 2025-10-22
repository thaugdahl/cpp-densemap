#include <algorithm>
#include <functional>
#include <iostream>
#include <random>
#include <thread>
#include <typeindex>
#include <vector>
#include <unordered_map>
#include <string>

template <class DenseMapT>
struct DenseMapIterator;

template <class Key>
struct DenseMapHashInfo {
    using type = decltype(std::declval<std::hash<Key>>()(std::declval<Key>()));
};

template <class Key>
struct DenseMapHash {
    using HashInfo = DenseMapHashInfo<Key>;
    using HashType = typename HashInfo::type;
    static constexpr HashType hash(const Key &key) {
        return std::hash<Key>{}(key);
    }
};

template <class Key>
struct DenseMapKeyInfo {
    // Interface
    static constexpr bool isTombstone(const Key &key);
    static constexpr Key getTombstone();

    static constexpr bool isEmpty(const Key &key);
    static constexpr Key getEmpty();

    static Key getDefault() {
        return Key{};
    };
};

template <class DMap>
struct DenseMapInfo;


struct TombstoneTypeIndex{};

template<>
struct DenseMapKeyInfo<std::type_index> {
    using Self = DenseMapKeyInfo<std::string>;
    using Key = std::type_index;
    static bool isTombstone(const Key &key) {
        return key == getTombstone();
    }

    static std::type_index getTombstone() {
        return Key(typeid(TombstoneTypeIndex));
    };


    static bool isEmpty(const Key &key) {
        return key == getEmpty();
    }

    static Key getEmpty() {
        return Key(typeid(void));
    }
};

template<>
struct DenseMapKeyInfo<std::string> {
    using Self = DenseMapKeyInfo<std::string>;
    static bool isTombstone(const std::string &key) {
        return key == getTombstone();
    }

    static std::string getTombstone() {
        return std::string{1, '\xFF'};
    };


    static bool isEmpty(const std::string &key) {
        return key == getEmpty();
    }

    static std::string getEmpty() {
        return std::string{};
    }
};


template <class DMap>
struct LinearProbe;


template<class Key, class Value, template <class Map> class Strategy = LinearProbe>
struct DenseMap : protected Strategy<DenseMap<Key, Value, Strategy>> {
    using Self = DenseMap<Key, Value, Strategy>;
    using KVPair = std::pair<Key, Value>;
    using Container = std::vector<KVPair>;
    using KeyT = Key;
    using ValueT = Value;

    /// The max load factor before we resize and rehash
    static constexpr double MAX_LOAD = 0.77;


    using ProbingStrategy = Strategy<DenseMap<KeyT, ValueT>>;
    friend ProbingStrategy;

    using iterator = DenseMapIterator<Self>;

    /// Constructors
    DenseMap() {
        storage.resize(8, KVPair{DenseMapKeyInfo<KeyT>::getEmpty(), ValueT{}});
    }

    ~DenseMap() = default;

    iterator insert(const KVPair &pair) {
        iterator available = this->find_available(pair.first);
        if ( available == end() ) return end();
        *available = pair;
        return available;
    }

    iterator insert(KVPair &&pair) {
        iterator available = this->find_available(pair.first);
        if ( available == end() ) return end();
        *available = std::move(pair);
        return available;
    }

    iterator find(const Key &key) {
        iterator result = this->find_impl(key);
        return result;
    }

    iterator erase(const Key &key) {
        iterator to_delete = find_impl(key);

        if ( to_delete != end() ) {
            *to_delete = makeTombstone();
        }
    }

    iterator erase(iterator iter) {
        if ( iter != end() ) {
            *iter = makeTombstone();
        }

        return iter;
    }

    iterator data() const {
        return iterator{storage.data(), storage.data() + storage.size()};
    }



    Value &operator[](const KeyT &key) {

        std::cout << "Maps to " << hash(key) % storage.size() << "\n";

        auto iter = find(key);
        if ( iter == end() ) {
            iter = this->find_available(key);
            if ( iter == end() ) throw std::runtime_error{"Couldn't insert!"};
            *iter = KVPair{key, ValueT{}};
            return iter->second;
        }

        return iter->second;
    }

    iterator begin() {
        // TODO -- should ideally point to a valid one

        auto vecIter = std::find_if(storage.begin(), storage.end(), [](const KVPair &pair) {
            return ! DenseMapKeyInfo<Key>::isEmpty(pair.first) && ! DenseMapKeyInfo<Key>::isTombstone(pair.first);
        });

        KVPair *endPtr = storage.data() + storage.size();
        if ( vecIter == storage.end() ) {
            return iterator{endPtr, endPtr};
        }

        KVPair *ptr = &(*vecIter);
        return iterator{ptr, endPtr};
    }

    iterator end() {
        KVPair *endPtr = &(*storage.end());
        return iterator{endPtr, endPtr};
    }


    KVPair makeTombstone() {
        return std::make_pair(DenseMapKeyInfo<KeyT>::getTombstone(), ValueT{});
    }


private:
    using HashType = DenseMapHashInfo<Key>;
    typename HashType::type hash(const Key &k) {
        return DenseMapHash<Key>::hash(k);
    }

    void resize_and_rehash() {
    }

    double loadFactor() {
        return static_cast<double>(countEmptyAndTombstones()) / static_cast<double>(storage.size());
    }



    //< Counts available slots.
    std::size_t countEmptyAndTombstones() {
        return std::count_if(storage.begin(), storage.end(), [](KVPair &kvPair) {
            return DenseMapKeyInfo<Key>::isEmpty(kvPair.first);
        });
    }

    Container storage;

    friend DenseMapIterator<Self>;
};

template <class DenseMapT>
struct DenseMapIterator {
    using Self = DenseMapIterator<DenseMapT>;

    using KeyT = typename DenseMapInfo<DenseMapT>::Key;
    using KVType = typename DenseMapInfo<DenseMapT>::KVPair;

    using iterator_category = std::random_access_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = KVType;
    using pointer = KVType *;
    using reference = KVType &;

    DenseMapIterator(pointer ptr_, pointer end_) : ptr{ptr_}, end{end_} {
    }


    reference operator*() const { return *ptr; }
    pointer operator->() { return ptr; }



    // Prefix
    Self &operator++() {
        pointer candidate = ptr;

        bool validCandidate = false;
        while ( !validCandidate && candidate != end ) {
            candidate++;

            bool isEmpty = DenseMapKeyInfo<KeyT>::isEmpty(candidate->first);
            bool isTombstone = DenseMapKeyInfo<KeyT>::isTombstone(candidate->first);

            if ( ! isEmpty && !isTombstone ) {
                ptr = candidate;
                return *this;
            }
        }

        ptr = end;
        return *this;
    }

    // Postfix
    Self operator++(int) {
        Self tmp = *this; ++(*this); return tmp;
    }


    friend bool operator==(const Self &a, const Self& b) {
        return a.ptr == b.ptr;
    }

    friend bool operator!=(const Self &a, const Self& b) {
        return a.ptr != b.ptr;
    }

private:
    pointer ptr;
    pointer end;
};

template <class DMap>
struct LinearProbe {

    using DenseMapIter = typename DenseMapInfo<DMap>::Iterator;
    using KeyT = typename DenseMapInfo<DMap>::Key;
    using ValueT = typename DenseMapInfo<DMap>::Value;
    using KVPair = typename::DenseMapInfo<DMap>::KVPair;

    DenseMapIter find_available(const KeyT &key) {
        DMap &map = *static_cast<DMap *>(this);


        std::size_t index = map.hash(key) % map.storage.size();
        std::size_t startIndex = index;

        KVPair *candidate = &map.storage[index];

        // TODO: Check what max attempts should be
        static constexpr std::size_t max_attempts = 1000;

        KVPair *endPtr = map.storage.data() + map.storage.size();

        auto canInsert = [](const KeyT &key) {
            return DenseMapKeyInfo<KeyT>::isEmpty(key) || DenseMapKeyInfo<KeyT>::isTombstone(key);
        };

        KVPair *result = candidate;

        bool first_round = true;

        while ( true ) {
            if ( candidate->first == key ) {
                throw std::runtime_error{"Cannot insert into a DenseMap where the key already exists!"};
            }

            if ( index == startIndex && !first_round) {
                result = endPtr;
                break;
            }

            if ( canInsert(candidate->first) ) {
                result = candidate;
                break;
            }

            first_round = false;
            index = (index + 1) % map.storage.size();
            candidate = &map.storage[index];
        }

        return DenseMapIter{result, endPtr};
    }

    // With lazy deletion
    DenseMapIter find_impl(const KeyT &key) {
        DMap &map = *static_cast<DMap *>(this);
        KVPair *firstTombstone = nullptr;
        std::size_t index = map.hash(key) % map.storage.size();
        std::size_t firstIndex = index;

        // Continue as long as there are tombstones
        KVPair *candidate = &map.storage[index];
        KVPair *endPtr = map.storage.data() + map.storage.size();

        constexpr bool (*isEmptyFn)(const KeyT &) = DenseMapKeyInfo<KeyT>::isEmpty;
        constexpr bool (*isTombstoneFn)(const KeyT &) = DenseMapKeyInfo<KeyT>::isTombstone;

        while ( ! isEmptyFn(candidate->first) ) {
            if ( isTombstoneFn(candidate->first) ) {
                if ( firstTombstone == nullptr ) {
                    firstTombstone = candidate;
                }

                index = (index + 1) % map.storage.size();
                candidate = &map.storage[index];
            } else if ( candidate->first == key ) {
                // Swap'em and return
                if ( firstTombstone != nullptr ) {
                    std::swap(*candidate, *firstTombstone);
                    std::swap(candidate, firstTombstone);
                }

                return DenseMapIter{candidate, endPtr};
            } else {
                index = (index + 1) % map.storage.size();
                candidate = &map.storage[index];
            }

            // Check if we've wrapped around.
            if ( index == firstIndex )
                return DenseMapIter{endPtr, endPtr};
        }

        return DenseMapIter{endPtr, endPtr};
    }
};

template <class KeyT, class ValueT, template <class DMap> class Strategy>
struct DenseMapInfo<DenseMap<KeyT, ValueT, Strategy>> : protected DenseMapKeyInfo<KeyT> {
    using Key = KeyT;
    using Value = ValueT;
    using KVPair = std::pair<Key, Value>;
    using Iterator = DenseMapIterator<DenseMap<KeyT, ValueT, Strategy>>;
};



template <class Callback>
struct MessageTopic {
    std::string identifier;
    // Unsubscribe how? Need to issue a subscription handle
};


struct MessageBus {
    template <class T>
    void subscribe(std::function<void(const T&)> handler) {
        auto &handlers = map_[std::type_index(typeid(T))];
        handlers.push_back([handler](const void *msg) {
            handler(*static_cast<const T*>(msg));
        });
    }

    template <class T>
    void publish(const T& message) {
        for ( auto fn : map_[std::type_index(typeid(T))] ) {
            fn(&message);
        }
    }


private:
    DenseMap<std::type_index, std::vector<std::function<void (const void *)>>> map_{};
};


struct PlayerHit {
    int dmg = 0;
};

struct EnteredRange {
    size_t id = 0;
};

int main(int argc, char *argv[])
{

    // DenseMap<std::string, int> mmap;

    // using iterator = decltype(mmap)::iterator;
    // std::vector<iterator> iterators{};

    //     std::size_t id = 0;

    // while ( true ) {
    //     std::string insertion_key = "key" + std::to_string(id);

    //     iterator res = mmap.insert(std::make_pair(insertion_key, static_cast<int>(id)));

    //     if ( res == mmap.end() ) {
    //         break;
    //     }

    //     iterators.emplace_back(res);
    //     id++;
    // }

    // std::random_device rd{};
    // std::mt19937 twister{rd()};
    // std::uniform_int_distribution<int> dist(0, 15);
    // for ( auto i = 0UL; i < 6UL; i++ ) {
    //     size_t index = dist(twister);
    //     mmap.erase(iterators[index]);
    // }

    // // Lookup all keys
    // for ( auto i = 0; i < 15; i++ ) {
    //     std::string insertion_key = "key" + std::to_string(i);
    //     iterator rr = mmap.find(insertion_key);

    //     if ( rr != mmap.end()) {
    //         std::cout << "On lookup found: " << rr->first <<
    //             ", " << rr->second << "\n";
    //     } else {
    //         std::cout << "end?" << "\n";
    //     }

    // }



    // for ( auto &[k,v] : mmap ) {
    //     std::cout << &k << "," << k << ", " << v <<"\n";
    // }

    MessageBus mb{};

    mb.subscribe<PlayerHit>([](const PlayerHit &ph) {
        std::cout << "Player was hit with: " << ph.dmg << " damage \n";
    });

    mb.subscribe<EnteredRange>([](const EnteredRange &er) {
        std::cout << "Entity with ID " << er.id << " entered range \n";
    });

    mb.publish(PlayerHit{20});
    mb.publish(EnteredRange{4});



    return 0;
}
