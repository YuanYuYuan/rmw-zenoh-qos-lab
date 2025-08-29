#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <limits>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>

#include "rcl/rcl.h"
#include "rcutils/cmdline_parser.h"
#include "rcutils/logging_macros.h"
#include "rcutils/time.h"
#include "rosidl_runtime_c/message_type_support_struct.h"
#include "std_msgs/msg/u_int8_multi_array.h"

void print_help(const char *program) {
    std::cout
        << "Usage: " << program
        << " [--mode pub|sub|parallel_pub] [--topic1 <name>] [--topic2 <name>] [--duration <sec>] [--rate1 <Hz>] [--rate2 <Hz>] [--payload1 <bytes>] [--payload2 <bytes>] [--help]\n";
}

bool parse_args(int argc, char *argv[], std::string &mode, std::string &topic1_name, std::string &topic2_name,
                double &duration, double &rate1, double &rate2, std::size_t &payload1, std::size_t &payload2) {
    mode = "sub";
    topic1_name = "topic_1";
    topic2_name = "topic_2";
    duration = 3.0;
    rate1 = 1.0;
    rate2 = 2.0;
    payload1 = 20;
    payload2 = 40;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            print_help(argv[0]);
            return false;
        } else if (arg == "--mode" && i + 1 < argc) {
            mode = argv[++i];
        } else if (arg == "--topic1" && i + 1 < argc) {
            topic1_name = argv[++i];
        } else if (arg == "--topic2" && i + 1 < argc) {
            topic2_name = argv[++i];
        } else if (arg == "--duration" && i + 1 < argc) {
            duration = std::stod(argv[++i]);
        } else if (arg == "--rate1" && i + 1 < argc) {
            rate1 = std::stod(argv[++i]);
        } else if (arg == "--rate2" && i + 1 < argc) {
            rate2 = std::stod(argv[++i]);
        } else if (arg == "--payload1" && i + 1 < argc) {
            payload1 = static_cast<std::size_t>(std::stoul(argv[++i]));
        } else if (arg == "--payload2" && i + 1 < argc) {
            payload2 = static_cast<std::size_t>(std::stoul(argv[++i]));
        } else {
            std::cerr << "Unknown arg: " << arg << "\n";
            print_help(argv[0]);
            return false;
        }
    }

    if (mode != "pub" && mode != "sub" && mode != "parallel_pub") {
        std::cerr << "Invalid --mode\n";
        return false;
    }
    return true;
}

std_msgs__msg__UInt8MultiArray create_message(size_t payload, uint8_t fill_byte, uint32_t msg_id) {
    static thread_local std::vector<uint8_t> base_payload1, base_payload2;
    static thread_local size_t last_payload1_size = 0, last_payload2_size = 0;
    static thread_local uint8_t last_fill_byte1 = 0, last_fill_byte2 = 0;

    std::vector<uint8_t>* current_base;
    size_t* last_size;
    uint8_t* last_fill;

    // Use different base payloads for different fill bytes to avoid conflicts
    if (fill_byte == 0xA1) {
        current_base = &base_payload1;
        last_size = &last_payload1_size;
        last_fill = &last_fill_byte1;
    } else {
        current_base = &base_payload2;
        last_size = &last_payload2_size;
        last_fill = &last_fill_byte2;
    }

    // Only recreate base payload if size or fill byte changed
    if (*last_size != payload || *last_fill != fill_byte) {
        current_base->resize(payload);
        std::fill(current_base->begin(), current_base->end(), fill_byte);
        *last_size = payload;
        *last_fill = fill_byte;
    }

    std_msgs__msg__UInt8MultiArray msg;
    std_msgs__msg__UInt8MultiArray__init(&msg);
    msg.data.size = payload;
    msg.data.capacity = payload;
    msg.data.data = static_cast<uint8_t *>(malloc(payload));

    // Copy the base payload
    memcpy(msg.data.data, current_base->data(), payload);

    // Update only the msg_id and timestamp
    memcpy(msg.data.data, &msg_id, sizeof(uint32_t));

    auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    if (payload >= sizeof(uint32_t) + sizeof(int64_t)) {
        memcpy(msg.data.data + sizeof(uint32_t), &timestamp, sizeof(int64_t));
    }

    return msg;
}

bool publish_message(rcl_publisher_t *publisher, std_msgs__msg__UInt8MultiArray *msg, const std::string &topic_name) {
    if (rcl_publish(publisher, msg, nullptr) == RCL_RET_OK) {
        return true;
    } else {
        RCUTILS_LOG_ERROR("rcl_publish to %s: %s", topic_name.c_str(), rcutils_get_error_string().str);
        return false;
    }
}

std::string format_bytes(size_t bytes) {
    if (bytes >= 1024 * 1024 * 1024) {
        return std::to_string(bytes / (1024 * 1024 * 1024)) + " GB";
    } else if (bytes >= 1024 * 1024) {
        return std::to_string(bytes / (1024 * 1024)) + " MB";
    } else if (bytes >= 1024) {
        return std::to_string(bytes / 1024) + " KB";
    } else {
        return std::to_string(bytes) + " B";
    }
}

void run_dual_publisher(rcl_node_t *node, const std::string &topic1, const std::string &topic2,
                        double duration, double rate1, double rate2, size_t payload1, size_t payload2) {
    rcl_publisher_t publisher1 = rcl_get_zero_initialized_publisher();
    rcl_publisher_t publisher2 = rcl_get_zero_initialized_publisher();
    const rosidl_message_type_support_t *ts = ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, UInt8MultiArray);

    rcl_publisher_options_t pub_opts = rcl_publisher_get_default_options();

    rcl_ret_t rc1 = rcl_publisher_init(&publisher1, node, ts, topic1.c_str(), &pub_opts);
    if (rc1 != RCL_RET_OK) {
        RCUTILS_LOG_ERROR("Failed to init publisher1: %s", rcutils_get_error_string().str);
        return;
    }

    rcl_ret_t rc2 = rcl_publisher_init(&publisher2, node, ts, topic2.c_str(), &pub_opts);
    if (rc2 != RCL_RET_OK) {
        RCUTILS_LOG_ERROR("Failed to init publisher2: %s", rcutils_get_error_string().str);
        if (rcl_publisher_fini(&publisher1, node) != RCL_RET_OK) {
            RCUTILS_LOG_ERROR("rcl_publisher_fini publisher1: %s", rcutils_get_error_string().str);
        }
        return;
    }

    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point next_pub1 = start;
    std::chrono::steady_clock::time_point next_pub2 = start;
    std::chrono::steady_clock::time_point last_status = start;

    double interval1_ms = 1000.0 / rate1;
    double interval2_ms = 1000.0 / rate2;

    size_t count1 = 0, count2 = 0;
    size_t count1_last_status = 0, count2_last_status = 0;
    uint32_t msg_id1 = 0, msg_id2 = 0;

    while (true) {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start).count();
        if (duration > 0.0 && elapsed >= duration) break;

        // Print status every 1 second
        auto time_since_status = std::chrono::duration<double>(now - last_status).count();
        if (time_since_status >= 1.0) {
            double current_rate1 = (count1 - count1_last_status) / time_since_status;
            double current_rate2 = (count2 - count2_last_status) / time_since_status;
            RCUTILS_LOG_INFO("Publishing: %s %zu msgs (%.1f Hz), %s %zu msgs (%.1f Hz)",
                           topic1.c_str(), count1, current_rate1,
                           topic2.c_str(), count2, current_rate2);
            count1_last_status = count1;
            count2_last_status = count2;
            last_status = now;
        }

        bool should_pub1 = now >= next_pub1;
        bool should_pub2 = now >= next_pub2;

        if (should_pub1) {
            auto msg = create_message(payload1, 0xA1, msg_id1);
            if (publish_message(&publisher1, &msg, topic1)) {
                count1++;
                msg_id1++;
            }
            std_msgs__msg__UInt8MultiArray__fini(&msg);
            next_pub1 = now + std::chrono::milliseconds((int)interval1_ms);
        }

        if (should_pub2) {
            auto msg = create_message(payload2, 0xB2, msg_id2);
            if (publish_message(&publisher2, &msg, topic2)) {
                count2++;
                msg_id2++;
            }
            std_msgs__msg__UInt8MultiArray__fini(&msg);
            next_pub2 = now + std::chrono::milliseconds((int)interval2_ms);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    RCUTILS_LOG_INFO("Published %zu messages to %s (%.1f Hz, %zu bytes) and %zu messages to %s (%.1f Hz, %zu bytes)",
                     count1, topic1.c_str(), rate1, payload1, count2, topic2.c_str(), rate2, payload2);

    if (rcl_publisher_fini(&publisher1, node) != RCL_RET_OK) {
        RCUTILS_LOG_ERROR("rcl_publisher_fini publisher1: %s", rcutils_get_error_string().str);
    }
    if (rcl_publisher_fini(&publisher2, node) != RCL_RET_OK) {
        RCUTILS_LOG_ERROR("rcl_publisher_fini publisher2: %s", rcutils_get_error_string().str);
    }
}

void publisher_thread(rcl_node_t *node, const std::string &topic_name, double duration, double rate,
                     size_t payload, uint8_t fill_byte, std::atomic<bool> &should_stop) {
    rcl_publisher_t publisher = rcl_get_zero_initialized_publisher();
    const rosidl_message_type_support_t *ts = ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, UInt8MultiArray);
    rcl_publisher_options_t pub_opts = rcl_publisher_get_default_options();

    rcl_ret_t rc = rcl_publisher_init(&publisher, node, ts, topic_name.c_str(), &pub_opts);
    if (rc != RCL_RET_OK) {
        RCUTILS_LOG_ERROR("Failed to init publisher for %s: %s", topic_name.c_str(), rcutils_get_error_string().str);
        return;
    }

    auto start = std::chrono::steady_clock::now();
    double interval_ms = 1000.0 / rate;
    auto next_pub = start;
    auto last_status = start;
    uint32_t msg_id = 0;
    size_t count = 0;
    size_t count_last_status = 0;

    while (!should_stop.load()) {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start).count();
        if (duration > 0.0 && elapsed >= duration) break;

        // Print status every 1 second
        auto time_since_status = std::chrono::duration<double>(now - last_status).count();
        if (time_since_status >= 1.0) {
            double current_rate = (count - count_last_status) / time_since_status;
            RCUTILS_LOG_INFO("Publishing %s: %zu msgs (%.1f Hz)",
                           topic_name.c_str(), count, current_rate);
            count_last_status = count;
            last_status = now;
        }

        if (now >= next_pub) {
            auto msg = create_message(payload, fill_byte, msg_id);
            if (publish_message(&publisher, &msg, topic_name)) {
                count++;
                msg_id++;
            }
            std_msgs__msg__UInt8MultiArray__fini(&msg);
            next_pub = now + std::chrono::milliseconds((int)interval_ms);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    RCUTILS_LOG_INFO("Thread for %s published %zu messages (%.1f Hz, %zu bytes)",
                     topic_name.c_str(), count, rate, payload);

    if (rcl_publisher_fini(&publisher, node) != RCL_RET_OK) {
        RCUTILS_LOG_ERROR("rcl_publisher_fini for %s: %s", topic_name.c_str(), rcutils_get_error_string().str);
    }
}

void run_parallel_publisher(rcl_node_t *node, const std::string &topic1, const std::string &topic2,
                           double duration, double rate1, double rate2, size_t payload1, size_t payload2) {
    std::atomic<bool> should_stop(false);

    std::thread thread1(publisher_thread, node, std::ref(topic1), duration, rate1, payload1, 0xA1, std::ref(should_stop));
    std::thread thread2(publisher_thread, node, std::ref(topic2), duration, rate2, payload2, 0xB2, std::ref(should_stop));

    if (duration > 0.0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(duration * 1000)));
    } else {
        thread1.join();
        thread2.join();
        return;
    }

    should_stop.store(true);
    thread1.join();
    thread2.join();
}

void run_dual_subscriber(rcl_node_t *node, const std::string &topic1, const std::string &topic2, double duration) {
    rcl_subscription_t subscription1 = rcl_get_zero_initialized_subscription();
    rcl_subscription_t subscription2 = rcl_get_zero_initialized_subscription();
    const rosidl_message_type_support_t *ts = ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, UInt8MultiArray);
    rcl_subscription_options_t sub_opts = rcl_subscription_get_default_options();

    rcl_ret_t rc1 = rcl_subscription_init(&subscription1, node, ts, topic1.c_str(), &sub_opts);
    if (rc1 != RCL_RET_OK) {
        RCUTILS_LOG_ERROR("Failed to init subscription1: %s", rcutils_get_error_string().str);
        return;
    }

    rcl_ret_t rc2 = rcl_subscription_init(&subscription2, node, ts, topic2.c_str(), &sub_opts);
    if (rc2 != RCL_RET_OK) {
        RCUTILS_LOG_ERROR("Failed to init subscription2: %s", rcutils_get_error_string().str);
        if (rcl_subscription_fini(&subscription1, node) != RCL_RET_OK) {
            RCUTILS_LOG_ERROR("rcl_subscription_fini subscription1: %s", rcutils_get_error_string().str);
        }
        return;
    }

    rcl_wait_set_t wait_set = rcl_get_zero_initialized_wait_set();
    if (rcl_wait_set_init(&wait_set, 2, 0, 0, 0, 0, 0, node->context, rcl_get_default_allocator()) != RCL_RET_OK) {
        RCUTILS_LOG_ERROR("rcl_wait_set_init: %s", rcutils_get_error_string().str);
        if (rcl_subscription_fini(&subscription1, node) != RCL_RET_OK) {
            RCUTILS_LOG_ERROR("rcl_subscription_fini subscription1: %s", rcutils_get_error_string().str);
        }
        if (rcl_subscription_fini(&subscription2, node) != RCL_RET_OK) {
            RCUTILS_LOG_ERROR("rcl_subscription_fini subscription2: %s", rcutils_get_error_string().str);
        }
        return;
    }

    auto start = std::chrono::steady_clock::now();
    auto last_rate_display = start;
    uint32_t count1 = 0, count2 = 0;
    uint32_t count1_last_second = 0, count2_last_second = 0;
    double latency_sum1 = 0.0, latency_sum2 = 0.0;
    uint32_t latency_count1 = 0, latency_count2 = 0;
    double latency_sum1_last_second = 0.0, latency_sum2_last_second = 0.0;
    uint32_t latency_count1_last_second = 0, latency_count2_last_second = 0;
    size_t payload_size1 = 0, payload_size2 = 0;

    uint32_t first_msg_id1 = 0, first_msg_id2 = 0;
    uint32_t last_msg_id1 = 0, last_msg_id2 = 0;
    uint32_t missed_events1 = 0, missed_events2 = 0;
    bool first_msg1 = true, first_msg2 = true;


    while (1) {
        auto now = std::chrono::steady_clock::now();
        if (duration > 0.0) {
            double elapsed = std::chrono::duration<double>(now - start).count();
            if (elapsed >= duration) break;
        }

        auto time_since_last_display = std::chrono::duration<double>(now - last_rate_display).count();
        if (time_since_last_display >= 1.0) {
            double rate1 = (count1 - count1_last_second) / time_since_last_display;
            double rate2 = (count2 - count2_last_second) / time_since_last_display;

            double avg_latency1 = std::numeric_limits<double>::quiet_NaN();
            double avg_latency2 = std::numeric_limits<double>::quiet_NaN();
            uint32_t msgs_with_latency1 = latency_count1 - latency_count1_last_second;
            uint32_t msgs_with_latency2 = latency_count2 - latency_count2_last_second;

            if (msgs_with_latency1 > 0) {
                avg_latency1 = (latency_sum1 - latency_sum1_last_second) / msgs_with_latency1;
            }
            if (msgs_with_latency2 > 0) {
                avg_latency2 = (latency_sum2 - latency_sum2_last_second) / msgs_with_latency2;
            }

            double loss_rate1;
            if (count1_last_second == count1) {
                loss_rate1 = 100.0;
            } else {
                uint32_t total_expected1 = (first_msg1) ? 0 : last_msg_id1 - first_msg_id1;
                loss_rate1 = (total_expected1 == 0) ? 0.0 : static_cast<double>(missed_events1) / total_expected1 * 100.0;
            }

            double loss_rate2;
            if (count2_last_second == count2) {
                loss_rate2 = 100.0;
            } else {
                uint32_t total_expected2 = (first_msg2) ? 0 : last_msg_id2 - first_msg_id2;
                loss_rate2 = (total_expected2 == 0) ? 0.0 : static_cast<double>(missed_events2) / total_expected2 * 100.0;
            }


            std::cout << topic1 << ": " << format_bytes(payload_size1) << ", " << std::fixed << std::setprecision(1) << rate1 << " Hz, "
                      << std::setprecision(2) << avg_latency1 << " ms, "
                      << "loss: " << std::setprecision(2) << loss_rate1 << "%, "
                      << topic2 << ": " << format_bytes(payload_size2) << ", " << std::setprecision(1) << rate2 << " Hz, "
                      << std::setprecision(2) << avg_latency2 << " ms, "
                      << "loss: " << std::setprecision(2) << loss_rate2 << "%" << std::endl;

            count1_last_second = count1;
            count2_last_second = count2;
            latency_sum1_last_second = latency_sum1;
            latency_sum2_last_second = latency_sum2;
            latency_count1_last_second = latency_count1;
            latency_count2_last_second = latency_count2;
            last_rate_display = now;
        }

        rcl_ret_t rc = rcl_wait_set_clear(&wait_set);
        if (rcl_wait_set_add_subscription(&wait_set, &subscription1, nullptr) != RCL_RET_OK) {
            RCUTILS_LOG_ERROR("rcl_wait_set_add_subscription1: %s", rcutils_get_error_string().str);
            break;
        }
        if (rcl_wait_set_add_subscription(&wait_set, &subscription2, nullptr) != RCL_RET_OK) {
            RCUTILS_LOG_ERROR("rcl_wait_set_add_subscription2: %s", rcutils_get_error_string().str);
            break;
        }

        rc = rcl_wait(&wait_set, RCL_MS_TO_NS(100));
        if (rc == RCL_RET_TIMEOUT) continue;

        if (wait_set.subscriptions[0] == &subscription1) {
            std_msgs__msg__UInt8MultiArray msg;
            std_msgs__msg__UInt8MultiArray__init(&msg);
            rc = rcl_take(&subscription1, &msg, nullptr, nullptr);
            if (rc == RCL_RET_OK) {
                count1++;
                payload_size1 = msg.data.size;

                if (msg.data.size >= sizeof(uint32_t)) {
                    uint32_t msg_id;
                    memcpy(&msg_id, msg.data.data, sizeof(uint32_t));
                    if (first_msg1) {
                        first_msg_id1 = msg_id;
                        last_msg_id1 = msg_id;
                        first_msg1 = false;
                    } else {
                        if (msg_id > last_msg_id1 + 1) {
                            missed_events1++;
                        }
                        last_msg_id1 = msg_id;
                    }
                }

                if (msg.data.size >= sizeof(uint32_t) + sizeof(int64_t)) {
                    int64_t send_timestamp;
                    memcpy(&send_timestamp, msg.data.data + sizeof(uint32_t), sizeof(int64_t));
                    auto recv_timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
                    double latency_ms = (recv_timestamp - send_timestamp) / 1e6;
                    latency_sum1 += latency_ms;
                    latency_count1++;
                }
            }
            std_msgs__msg__UInt8MultiArray__fini(&msg);
        }

        if (wait_set.subscriptions[1] == &subscription2) {
            std_msgs__msg__UInt8MultiArray msg;
            std_msgs__msg__UInt8MultiArray__init(&msg);
            rc = rcl_take(&subscription2, &msg, nullptr, nullptr);
            if (rc == RCL_RET_OK) {
                count2++;
                payload_size2 = msg.data.size;

                if (msg.data.size >= sizeof(uint32_t)) {
                    uint32_t msg_id;
                    memcpy(&msg_id, msg.data.data, sizeof(uint32_t));
                    if (first_msg2) {
                        first_msg_id2 = msg_id;
                        last_msg_id2 = msg_id;
                        first_msg2 = false;
                    } else {
                        if (msg_id > last_msg_id2 + 1) {
                            missed_events2++;
                        }
                        last_msg_id2 = msg_id;
                    }
                }

                if (msg.data.size >= sizeof(uint32_t) + sizeof(int64_t)) {
                    int64_t send_timestamp;
                    memcpy(&send_timestamp, msg.data.data + sizeof(uint32_t), sizeof(int64_t));
                    auto recv_timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
                    double latency_ms = (recv_timestamp - send_timestamp) / 1e6;
                    latency_sum2 += latency_ms;
                    latency_count2++;
                }
            }
            std_msgs__msg__UInt8MultiArray__fini(&msg);
        }
    }

    RCUTILS_LOG_INFO("Received %d messages from %s and %d messages from %s",
                     count1, topic1.c_str(), count2, topic2.c_str());

    if (rcl_wait_set_fini(&wait_set) != RCL_RET_OK) {
        RCUTILS_LOG_ERROR("rcl_wait_set_fini: %s", rcutils_get_error_string().str);
    }
    if (rcl_subscription_fini(&subscription1, node) != RCL_RET_OK) {
        RCUTILS_LOG_ERROR("rcl_subscription_fini subscription1: %s", rcutils_get_error_string().str);
    }
    if (rcl_subscription_fini(&subscription2, node) != RCL_RET_OK) {
        RCUTILS_LOG_ERROR("rcl_subscription_fini subscription2: %s", rcutils_get_error_string().str);
    }
}

int main(int argc, char *argv[]) {
    rcl_ret_t rc;
    rcl_context_t context = rcl_get_zero_initialized_context();
    rcl_init_options_t init_opts = rcl_get_zero_initialized_init_options();
    rc = rcl_init_options_init(&init_opts, rcl_get_default_allocator());
    rc = rcl_init(argc, argv, &init_opts, &context);

    rcl_node_t node = rcl_get_zero_initialized_node();
    rcl_node_options_t node_opts = rcl_node_get_default_options();
    rc = rcl_node_init(&node, "dual_pubsub_rcl_node", "", &context, &node_opts);

    std::string mode, topic1, topic2;
    double duration, rate1, rate2;
    size_t payload1, payload2;
    if (!parse_args(argc, argv, mode, topic1, topic2, duration, rate1, rate2, payload1, payload2)) {
        if (rcl_shutdown(&context) != RCL_RET_OK) {
            RCUTILS_LOG_ERROR("rcl_shutdown: %s", rcutils_get_error_string().str);
            return -1;
        }
        return 1;
    }

    if (mode == "pub") {
        run_dual_publisher(&node, topic1, topic2, duration, rate1, rate2, payload1, payload2);
    } else if (mode == "parallel_pub") {
        run_parallel_publisher(&node, topic1, topic2, duration, rate1, rate2, payload1, payload2);
    } else {
        run_dual_subscriber(&node, topic1, topic2, duration);
    }

    if (rcl_node_fini(&node) != RCL_RET_OK) {
        RCUTILS_LOG_ERROR("rcl_node_fini: %s", rcutils_get_error_string().str);
        return -1;
    }

    if (rcl_shutdown(&context) != RCL_RET_OK) {
        RCUTILS_LOG_ERROR("rcl_shutdown: %s", rcutils_get_error_string().str);
        return -1;
    }
    return 0;
}
