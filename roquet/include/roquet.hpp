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
// more work is needed, e.g. transactional pushes (storing the tail position and the transaction sequence
// alongside in a persistent memory and with using the PENDING flag). For data protection maybe some ideas
// of Linux RCU mechanism can be borrowed.
// TODO: evaluate which queue Wayland IPC used; potentially a FIFO since it is not allowed to lose commands
// TODO: evaluate whether more of the ideas from BuRiTTO can be combined with RoQueT or whether BuRiTTO can be made resilient
template <typename T, uint64_t Capacity>
class RoQueT {
public:
    static_assert(std::is_trivially_copyable_v<T>,
                  "T must be trivially copyable"); // TODO this restriction might be too strict; have to think about the use case

    static constexpr uint64_t InternalCapacity {Capacity + 2};

    static constexpr uint8_t EMPTY {0x01};
    static constexpr uint8_t PENDING {0x02};
    static constexpr uint8_t DATA {0x04};
    static constexpr uint8_t OVERFLOW {0x08};
    static constexpr uint8_t INSPECTED {0x10};
    static constexpr uint8_t END {0x80};

    RoQueT() {
        for (auto& idx : stateBuffer) {
            idx.store(EMPTY, std::memory_order_relaxed);
        }
        stateBuffer[1].store(END, std::memory_order_relaxed);
    }

    RoQueT(const RoQueT&) = delete;
    RoQueT(RoQueT&&)      = delete;

    RoQueT& operator=(const RoQueT&) = delete;
    RoQueT& operator=(RoQueT&&)      = delete;

private:
    class Producer {
    public:
        std::optional<T> push(const T& data) { return roquet.push(data, tailPosition); }

        bool empty() {
            auto preceedingPosition = tailPosition;
            if (preceedingPosition == 0) { preceedingPosition = RoQueT::InternalCapacity; }
            --preceedingPosition;

            return (roquet.stateBuffer[preceedingPosition].load(std::memory_order_relaxed) & RoQueT::DATA) == 0;
        }

        friend class RoQueT;

    private:
        Producer(RoQueT& r)
            : roquet(r) {}

    private:
        RoQueT&  roquet;
        uint32_t tailPosition {1};
    };

    class Consumer {
    public:
        std::optional<T> pop() { return roquet.pop(headPosition); }

        bool empty() {
            auto isCurrentEmpty = [this] {
                auto currentPosition = this->headPosition;
                return this->roquet.stateBuffer[currentPosition].load(std::memory_order_relaxed) & RoQueT::EMPTY;
            };

            auto isNextEndOrPending = [this] {
                auto nextPosition = this->headPosition;
                ++nextPosition;
                if (nextPosition == RoQueT::InternalCapacity) { nextPosition = 0; }
                auto state = this->roquet.stateBuffer[nextPosition].load(std::memory_order_relaxed);
                return (state & (RoQueT::END | RoQueT::PENDING));
            };

            return (isCurrentEmpty() && isNextEndOrPending());
        }

        friend class RoQueT;

    private:
        Consumer(RoQueT& r)
            : roquet(r) {}

    private:
        const RoQueT& roquet;
        uint32_t      headPosition {0};
    };

public:
    // lets have a similar interface like the Rust channels

    // TODO return optional<Producer> and ensure that a nullopt is returned after the second call
    Producer producer() { return Producer(*this); }

    // TODO return optional<Consumer> and ensure that a nullopt is returned after the second call
    Consumer consumer() { return Consumer(*this); }

private:
    // TODO use tuple instead of out-parameter
    std::optional<T> push(const T& data, uint32_t& position) {
        assert(position < InternalCapacity && "Position out of bounds");

        // NOTE: don't return nullopt but always resource to make use of NRVO
        std::optional<T> resource;
        auto             currentPosition = position;
        auto             nextPosition    = currentPosition + 1;
        if (nextPosition >= InternalCapacity) { nextPosition = 0; }

        uint8_t newState      = END | OVERFLOW;
        uint8_t expectedState = DATA;

        constexpr bool KEEP_TRYING {true};
        do {
            if (stateBuffer[nextPosition].compare_exchange_strong(expectedState, newState, std::memory_order_relaxed)) {
                if (expectedState & DATA) { resource.emplace(dataBuffer[nextPosition]); }
                break;
            }

            if (expectedState & DATA) {
                newState = END | OVERFLOW;
            } else {
                newState = END;
            }
        } while (KEEP_TRYING);

        if (!(stateBuffer[nextPosition].load(std::memory_order_relaxed) & END)) {
            // at this point the state at the next tail position should contain the END flag
            // TODO use an expected to indicate a fishy state of the queue
            resource.reset();
            return resource;
        }

        dataBuffer[currentPosition] = data;
        newState                    = stateBuffer[currentPosition].load(std::memory_order_relaxed);
        newState                    = DATA;
        stateBuffer[currentPosition].store(newState, std::memory_order_release);

        position = nextPosition;
        return resource;
    }

    // it is not nice to have this as const method but required to ensure the pop cannot mutate the data buffer ... let's pretend this works the same like
    // interior mutability with Rust atomics
    // TODO use tuple instead of out-parameter
    std::optional<T> pop(uint32_t& position) const {
        assert(position < InternalCapacity && "Position out of bounds");

        // NOTE: don't return nullopt but always resource to make use of NRVO
        std::optional<T> resource;
        auto             currentPosition = position;
        auto             nextPosition    = currentPosition + 1;

        constexpr bool KEEP_TRYING {true};
        uint64_t       loopCounter {0};
        do {
            ++loopCounter;
            if (loopCounter > 10000) {
                // a pop operation should not be unsuccessful with so many attempts
                // TODO better termination criteria, e.g. configurable number of wrap-arounds defined by the user
                // TODO use an expected to indicate a fishy state of the queue
                resource.reset();
                return resource;
            }

            if (nextPosition >= InternalCapacity) { nextPosition = 0; }

            auto stateNextPosition    = stateBuffer[nextPosition].load(std::memory_order_acquire);
            auto stateCurrentPosition = stateBuffer[currentPosition].load(std::memory_order_acquire);

            if ((stateCurrentPosition & EMPTY) && (stateNextPosition & (END | PENDING))) {
                resource.reset();
                // queue is empty
                break;
            }

            // set the inspected flag to prevent the ABA problem on a wrap-around;
            // the inspected flag can only be set by the consumer and will be reset by the producer when new data is pushed
            if (!(stateNextPosition & INSPECTED)) {
                auto expectedStateNextPosition = stateNextPosition;
                stateNextPosition |= INSPECTED;
                auto casSuccessful = stateBuffer[nextPosition].compare_exchange_strong(
                    expectedStateNextPosition, stateNextPosition | INSPECTED, std::memory_order_release, std::memory_order_acquire);
                if (!casSuccessful) { continue; }
            }

            resource.emplace(dataBuffer[nextPosition]);

            stateCurrentPosition = stateBuffer[currentPosition].load(std::memory_order_seq_cst);
            // TODO in theory the compare_exchange_strong with memory_order_release should have the same effect as the load with memory_order_seq_cst; further
            // investigations are needed to determine the performance impact and correctness
            // stateBuffer[currentPosition].compare_exchange_strong(stateCurrentPosition, stateCurrentPosition,std::memory_order_release);

            if ((stateCurrentPosition & END) && (stateCurrentPosition & OVERFLOW)) {
                stateBuffer[currentPosition].compare_exchange_strong(stateCurrentPosition, stateCurrentPosition & ~OVERFLOW, std::memory_order_release);
            } else if (((stateCurrentPosition & EMPTY) || (stateCurrentPosition & END)) && (stateNextPosition & DATA)) {
                auto newStateNextPosition = EMPTY;

                auto popSuccessful = stateBuffer[nextPosition].compare_exchange_strong(
                    stateNextPosition, newStateNextPosition, std::memory_order_release, std::memory_order_acquire);
                if (!popSuccessful) {
                    // find new END
                    currentPosition = nextPosition;
                    ++nextPosition;
                } else {
                    position = nextPosition;
                    if ((stateCurrentPosition & END) && (stateCurrentPosition & OVERFLOW)) {
                        // TODO inform the user about the overflow, e.g. by setting a flag or via return value
                    }
                    break;
                }
            } else {
                // there was an overflow and we need to find the new head
                currentPosition = nextPosition;
                ++nextPosition;
            }
        } while (KEEP_TRYING);

        return resource;
    }

private:
    mutable std::atomic<uint8_t> stateBuffer[InternalCapacity];
    // this could also be placed at a location where the consumer has no write access
    T dataBuffer[InternalCapacity];
    // tailPosition could be buffered here instead of in the 'Producer' to enable crash recovery
};

#endif // _ROQUET_HPP_
