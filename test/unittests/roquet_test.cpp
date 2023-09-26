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
    constexpr std::uint32_t ContainerCapacity { 10 };
    using DataType = size_t;
    using RoQueT = RoQueT<DataType, ContainerCapacity>;

    constexpr DataType RoQueT_CounterStartValue { 0 };

    DataType dataCounter { RoQueT_CounterStartValue };
    DataType pushCounter { RoQueT_CounterStartValue };

    GIVEN("An RoQueT with a fixed capacity") {
        RoQueT roquet;
        auto producer = roquet.producer();
        auto consumer = roquet.consumer();

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
                auto pushReturnValue = producer.push(DATA);
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
            bool producerEmptyReturnValue { false };
            bool consumerEmptyReturnValue { false };
            constexpr size_t ExtraCapacity = 1;
            for(auto i = 0u; i < ContainerCapacity + ExtraCapacity; i++) {

                pushReturnValue = producer.push(pushCounter);
                producerEmptyReturnValue = producer.empty();
                consumerEmptyReturnValue = consumer.empty();
                pushCounter++;

                if(pushReturnValue.has_value()) { break; }
                if(producerEmptyReturnValue == true) { break; }
                if(consumerEmptyReturnValue == true) { break; }
            }

            THEN("it should not overrun, return data or be empty") {
                REQUIRE(pushReturnValue.has_value() == false);
                REQUIRE(producerEmptyReturnValue == false);
                REQUIRE(consumerEmptyReturnValue == false);
            }

            AND_WHEN("pushing more data") {
                pushReturnValue = producer.push(pushCounter);
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
                    for(auto i = 0u; i < ContainerCapacity + ExtraCapacity; i++) {
                        producerEmptyReturnValue = producer.empty();
                        consumerEmptyReturnValue = consumer.empty();
                        popReturnValue = consumer.pop();
                        dataCounter++;

                        if(popReturnValue.has_value() == false) { break; }
                        if(producerEmptyReturnValue == true) { break; }
                        if(consumerEmptyReturnValue == true) { break; }
                        if(popReturnValue.value() != dataCounter-1) { break; }
                    }

                    THEN("it should always return data and not be empty") {
                        REQUIRE(popReturnValue.has_value() == true);
                        REQUIRE(popReturnValue.value() == dataCounter-1);
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
