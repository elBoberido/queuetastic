// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2023 Mathias Kraus <elboberido@m-hias.de>

#ifndef _ROQUET_HPP_
#define _ROQUET_HPP_

#include <atomic>
#include <cassert>
#include <cstdint>
#include <optional>
#include <type_traits>

#include <iostream>

// Robust Queue Transfer
//
// A proof-of-concept for a robust queue which could be used for e.g. a zero copy dbus implementation.
// This queue might protect against Murphy but not against Machiavelly. In order to protect against
// Machiavelly, some parts of the queue must be managed by a kernel (eBPF?). Additionally this can ensure
// resilience against crashes and enable processes to restart and continue with its operation. While
// the queue is just a building block and further measures have to be taken. Some ideas might be
// borrowed from the latest Wayland feature to recover from crashed compositors.
// Another use case might be Wayland IPC to let clients survive compositor crashes. For this, some
// more work is needed, e.g. transactional pushes (storing the write index and the transaction sequence
// alongside in a persistent memory and with using the PENDING flag). For data protection maybe some ideas
// of Linux RCU mechanism can be borrowed.
// TODO: evaluate which queue Wayland IPC used; potentially a FIFO since it is not allowed to lose commands
// TODO: evaluate whether more of the ideas from BuRiTTO can be combined with RoQueT or whether BuRiTTO can be made resilient
template <typename T, uint64_t Capacity>
class RoQueT {
public:
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable"); // TODO this restriction might be too strict; have to think about the use case

    static constexpr uint64_t InternalCapacity {Capacity + 2};

    static constexpr uint8_t EMPTY {0x01};
    static constexpr uint8_t PENDING {0x02};
    static constexpr uint8_t DATA {0x04};
    static constexpr uint8_t OVERFLOW {0x08}; // maybe not needed
    static constexpr uint8_t INSPECTED {0x10};
    static constexpr uint8_t END {0x80};

    RoQueT() {
        for(auto& idx: stateBuffer) {
            idx.store(EMPTY, std::memory_order_relaxed);
        }
        stateBuffer[1].store(END, std::memory_order_relaxed);
    }

    RoQueT(const RoQueT&) = delete;
    RoQueT(RoQueT&&) = delete;

    RoQueT& operator=(const RoQueT&) = delete;
    RoQueT& operator=(RoQueT&&) = delete;

private:
    class Producer {
    public:
        std::optional<T> push(const T& data) {
            return roquet.push(data, writeIndex);
        }

        bool empty() {
            auto index = writeIndex;
            if (index == 0) { index = RoQueT::InternalCapacity; }
            --index;

            return (roquet.stateBuffer[index].load(std::memory_order_relaxed) & RoQueT::DATA) == 0;
        }

    friend class RoQueT;

    private:
        Producer(RoQueT& r) : roquet(r) {}

    private:
        RoQueT& roquet;
        uint32_t writeIndex{1};
    };

    class Consumer {
    public:
        std::optional<T> pop() {
            return roquet.pop(readIndex);
        }

        bool empty() {
            auto index = readIndex;
            bool isCurrentEmpty = roquet.stateBuffer[index].load(std::memory_order_relaxed) & RoQueT::EMPTY;

            ++index;
            if (index == RoQueT::InternalCapacity) { index = 0; }
            auto state = roquet.stateBuffer[index].load(std::memory_order_relaxed);
            bool isNextEndOrPending = (state & (RoQueT::END | RoQueT::PENDING));

            return (isCurrentEmpty && isNextEndOrPending);
        }

    friend class RoQueT;

    private:
        Consumer(RoQueT& r) : roquet(r) {}

    private:
        const RoQueT& roquet;
        uint32_t readIndex{0};
    };

public:
    // lets have a similar interface like the Rust channels

    // TODO return optional<Producer> and ensure that a nullopt is returned after the second call
    Producer producer() {
        return Producer(*this);
    }

    // TODO return optional<Consumer> and ensure that a nullopt is returned after the second call
    Consumer consumer() {
        return Consumer(*this);
    }

private:
    // TODO use tuple instead of out-parameter
    std::optional<T> push(const T& data, uint32_t& index) {
        assert(index < InternalCapacity && "Index out of bounds");

        // NOTE: don't return nullopt but always resource to make use of NRVO
        std::optional<T> resource;
        auto nextIndex = index + 1;
        if (nextIndex >= InternalCapacity) { nextIndex = 0; }

        auto state = END; // TODO do we want to encode the overflow state? In this case push will not be wait-free anymore and a CAS needs to be performed
        state = stateBuffer[nextIndex].exchange(state, std::memory_order_relaxed);
        if (state & DATA) {
            resource.emplace(dataBuffer[nextIndex]);
        }

        dataBuffer[index] = data;
        state = stateBuffer[index].load(std::memory_order_relaxed);
        state = DATA;
        stateBuffer[index].store(state, std::memory_order_release);

        index = nextIndex;
        return resource;
    }

    // it is not nice to have this as const method but required to ensure the pop cannot mutate the data buffer ... let's pretend this works the same like interior mutability with Rust atomics
    // TODO use tuple instead of out-parameter
    std::optional<T> pop(uint32_t& index) const {
        assert(index < InternalCapacity && "Index out of bounds");
        // acquire read of the state at non-EMPTY
        // do a relaxed(acquire?) CAS at index of (non-EMPTY - 1, i.e. current index) with an expected EMPTY state to enforce memory sync
        // read the data and write (relaxed?) an EMPTY tag into the non-EMPTY state with a CAS

        // NOTE: don't return nullopt but always resource to make use of NRVO
        std::optional<T> resource;
        auto currentIndex = index;
        auto nextIndex = currentIndex + 1;
        if (nextIndex >= InternalCapacity) { nextIndex = 0; }

        auto state = stateBuffer[nextIndex].load(std::memory_order_relaxed);
        // TODO do we need an overflow flag
        if(state & END) {
            return resource;
        }

        return resource;
    }

private:
    mutable std::atomic<uint8_t> stateBuffer[InternalCapacity];
    // this could also be placed at a location where the consumer has no write access
    T dataBuffer[InternalCapacity];
    // writeIndex could be buffered here instead of Producer to enable crash recovery
};

#endif // _ROQUET_HPP_
