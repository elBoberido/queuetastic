// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2023 Mathias Kraus <elboberido@m-hias.de>

#include "roquet.hpp"

#define CATCH_CONFIG_ENABLE_BENCHMARKING
#include "catch.hpp"

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

SCENARIO("RoQuet - Unittest") {
    constexpr std::uint32_t ContainerCapacity {10};
    using DataType = size_t;
    using RoQueT   = RoQueT<DataType, ContainerCapacity>;

    constexpr DataType RoQueT_CounterStartValue {0};

    DataType dataCounter {RoQueT_CounterStartValue};
    DataType pushCounter {RoQueT_CounterStartValue};

    GIVEN("An RoQueT with a fixed capacity") {
        RoQueT roquet;
        auto   producer = roquet.producer();
        auto   consumer = roquet.consumer();

        WHEN("the roquet was just created") {
            THEN("it should be empty") {
                REQUIRE(producer.empty() == true);
                REQUIRE(consumer.empty() == true);
            }

            AND_WHEN("calling pop") {
                auto popReturnValue = consumer.pop();
                THEN("it should not return data") {
                    REQUIRE(popReturnValue.has_value() == false);
                }
            }

            AND_WHEN("pushing data") {
                constexpr DataType DATA {42};
                auto               pushReturnValue = producer.push(DATA);
                THEN("it should not be empty and not return data") {
                    REQUIRE(producer.empty() == false);
                    REQUIRE(consumer.empty() == false);
                    REQUIRE(pushReturnValue.has_value() == false);
                }

                AND_WHEN("calling pop") {
                    auto popReturnValue = consumer.pop();
                    THEN("it should return data and be empty again") {
                        REQUIRE(producer.empty() == true);
                        REQUIRE(consumer.empty() == true);
                        REQUIRE(popReturnValue.has_value() == true);
                        REQUIRE(popReturnValue.value() == DATA);
                    }
                }
            }
        }

        WHEN("filling the roquet to the point before overrun") {
            std::optional<DataType> pushReturnValue;
            bool                    producerEmptyReturnValue {false};
            bool                    consumerEmptyReturnValue {false};
            constexpr size_t        ExtraCapacity = 1;
            for (auto i = 0u; i < ContainerCapacity + ExtraCapacity; i++) {
                pushReturnValue          = producer.push(pushCounter);
                producerEmptyReturnValue = producer.empty();
                consumerEmptyReturnValue = consumer.empty();
                pushCounter++;

                if (pushReturnValue.has_value()) { break; }
                if (producerEmptyReturnValue == true) { break; }
                if (consumerEmptyReturnValue == true) { break; }
            }

            THEN("it should not overrun, return data or be empty") {
                REQUIRE(pushReturnValue.has_value() == false);
                REQUIRE(producerEmptyReturnValue == false);
                REQUIRE(consumerEmptyReturnValue == false);
            }

            AND_WHEN("pushing more data") {
                pushReturnValue          = producer.push(pushCounter);
                producerEmptyReturnValue = producer.empty();
                consumerEmptyReturnValue = consumer.empty();
                pushCounter++;

                THEN("it should overrun, return data and not be empty") {
                    REQUIRE(pushReturnValue.has_value() == true);
                    REQUIRE(pushReturnValue.value() == dataCounter);
                    REQUIRE(producerEmptyReturnValue == false);
                    REQUIRE(consumerEmptyReturnValue == false);
                }

                dataCounter++;

                AND_WHEN("pop all data out") {
                    std::optional<DataType> popReturnValue;
                    for (auto i = 0u; i < ContainerCapacity + ExtraCapacity; i++) {
                        producerEmptyReturnValue = producer.empty();
                        consumerEmptyReturnValue = consumer.empty();
                        popReturnValue           = consumer.pop();
                        dataCounter++;

                        if (popReturnValue.has_value() == false) { break; }
                        if (producerEmptyReturnValue == true) { break; }
                        if (consumerEmptyReturnValue == true) { break; }
                        if (popReturnValue.value() != dataCounter - 1) { break; }
                    }

                    THEN("it should always return data and not be empty") {
                        REQUIRE(popReturnValue.has_value() == true);
                        REQUIRE(popReturnValue.value() == dataCounter - 1);
                        REQUIRE(producerEmptyReturnValue == false);
                        REQUIRE(consumerEmptyReturnValue == false);
                    }

                    AND_WHEN("all data is out") {
                        THEN("it should be empty") {
                            REQUIRE(producer.empty() == true);
                            REQUIRE(consumer.empty() == true);
                            popReturnValue = consumer.pop();
                            REQUIRE(popReturnValue.has_value() == false);
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("RoQueT - Stress", "[.stress]") {
    constexpr std::uint32_t ContainerCapacity {10};
    using DataType = uint64_t;
    using RoQueT   = RoQueT<DataType, ContainerCapacity>;

    constexpr uint64_t NUMBER_OF_PUSHES {1000000};
    constexpr DataType COUNTER_START_VALUE {0};

    DataType          pushCounter {COUNTER_START_VALUE};
    DataType          popCounter {COUNTER_START_VALUE};
    DataType          overrunCounter {0};
    std::atomic<bool> pushThreadFinished {false};

    RoQueT roquet;
    auto   producer = roquet.producer();
    auto   consumer = roquet.consumer();

    std::vector<DataType> overrunData;
    std::vector<DataType> popData;
    overrunData.reserve(NUMBER_OF_PUSHES);
    popData.reserve(NUMBER_OF_PUSHES);

    std::atomic<uint64_t>   threadRunCount {0};
    std::mutex              mtx;
    std::condition_variable condVar;
    bool                    run {false};

    auto pushThread = std::thread([&] {
        {
            std::unique_lock<std::mutex> lock(mtx);
            threadRunCount.fetch_add(1);
            condVar.wait(lock, [&]() -> bool { return run; });
        }
        std::string msg {"Thread push: on CPU "};
        msg += std::to_string(sched_getcpu());
        msg += "\n";
        std::cout << msg << std::flush;
        for (uint64_t i = 0; i < NUMBER_OF_PUSHES; i++) {
            const auto retVal = producer.push(pushCounter);
            pushCounter++;
            if (retVal.has_value()) {
                overrunCounter++;
                overrunData.push_back(retVal.value());
            }
        }

        msg = "Thread push finished\n";
        std::cout << msg << std::flush;
        pushThreadFinished = true;
    });

    auto popThread = std::thread([&] {
        {
            std::unique_lock<std::mutex> lock(mtx);
            threadRunCount.fetch_add(1);
            condVar.wait(lock, [&]() -> bool { return run; });
        }
        std::string msg {"Thread pop: on CPU "};
        msg += std::to_string(sched_getcpu());
        msg += "\n";
        std::cout << msg << std::flush;
        uint64_t failedPopsWhilePushThreadFinished {0};
        while (!pushThreadFinished.load(std::memory_order_relaxed) || !consumer.empty()) {
            auto retVal = consumer.pop();
            if (retVal.has_value()) {
                popCounter++;
                popData.push_back(retVal.value());
            } else if (pushThreadFinished.load(std::memory_order_relaxed)) {
                ++failedPopsWhilePushThreadFinished;
                if (failedPopsWhilePushThreadFinished > ContainerCapacity * 2) {
                    msg = "Thread pop detected error\n";
                    std::cout << msg << std::flush;
                    break;
                }
            }
        }
        msg = "Thread pop finished\n";
        std::cout << msg << std::flush;
    });

    // set CPU affinity
    // fastest option is when both threads run on the same core but with hyperthreading
    // cpu_set_t cpuset;
    // CPU_ZERO(&cpuset);
    // CPU_SET(0, &cpuset);
    // pthread_setaffinity_np(pushThread.native_handle(), sizeof(cpu_set_t), &cpuset);
    // CPU_ZERO(&cpuset);
    // CPU_SET(1, &cpuset);
    // pthread_setaffinity_np(popThread.native_handle(), sizeof(cpu_set_t), &cpuset);

    while (threadRunCount < 2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    {
        std::unique_lock<std::mutex> lock(mtx);
        run = true;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    condVar.notify_all();

    pushThread.join();
    popThread.join();

    auto elapsedTime = std::chrono::high_resolution_clock::now() - startTime;
    std::cout << "duration: " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsedTime).count() << "ms" << std::endl;

    std::cout << "expected pushes \t" << NUMBER_OF_PUSHES << std::endl;
    std::cout << "push counter \t" << pushCounter << std::endl;
    std::cout << "overrun + pop \t" << overrunCounter + popCounter << std::endl;
    std::cout << "overrun counter \t" << overrunCounter << std::endl;
    std::cout << "pop counter \t" << popCounter << std::endl;

    std::cout << "overrun values: \t";
    int i = 0;
    for (auto& data : overrunData) {
        std::cout << data << " ";
        i++;
        if (i > 100) { break; }
    }
    std::cout << std::endl;

    std::cout << "pop values: \t";
    i = 0;
    for (auto& data : popData) {
        std::cout << data << " ";
        i++;
        if (i > 100) { break; }
    }
    std::cout << std::endl;

    size_t overrunIndex = 0;
    size_t popIndex     = 0;
    bool   dataIntact   = true;
    for (size_t i = COUNTER_START_VALUE; i < pushCounter; i++) {
        if (overrunIndex < overrunData.size() && overrunData[overrunIndex] == i) {
            overrunIndex++;
        } else if (popIndex < popData.size() && popData[popIndex] == i) {
            popIndex++;
        } else {
            std::cout << "data loss detected at index: " << i << std::endl;
            dataIntact = false;
            break;
        }
    }

    CHECK(dataIntact);
    CHECK(NUMBER_OF_PUSHES == pushCounter);
    CHECK(pushCounter == (overrunCounter + popCounter));
}
