#include <getopt.h>
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
#include <memory>

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/u_int8_multi_array.hpp>

using std::placeholders::_1;

class DualPubSubNode : public rclcpp::Node {
public:
    DualPubSubNode(const std::string &mode, const std::string &topic1_name, const std::string &topic2_name,
                   double duration, double rate1, double rate2, std::size_t payload1, std::size_t payload2)
        : Node("dual_pubsub_cpp_node"),
          mode_(mode),
          topic1_name_(topic1_name),
          topic2_name_(topic2_name),
          duration_(duration),
          rate1_(rate1),
          rate2_(rate2),
          payload1_(payload1),
          payload2_(payload2),
          finished_(false),
          count1_(0),
          count2_(0),
          msg_id1_(0),
          msg_id2_(0) {
        
        if (mode_ == "pub") {
            setup_dual_publisher();
        } else if (mode_ == "parallel_pub") {
            setup_parallel_publisher();
        } else {
            setup_dual_subscriber();
        }
    }

private:
    std::string mode_;
    std::string topic1_name_;
    std::string topic2_name_;
    double duration_;
    double rate1_;
    double rate2_;
    std::size_t payload1_;
    std::size_t payload2_;
    bool finished_;
    
    std::atomic<size_t> count1_;
    std::atomic<size_t> count2_;
    std::atomic<uint32_t> msg_id1_;
    std::atomic<uint32_t> msg_id2_;
    
    // Publisher components
    rclcpp::Publisher<std_msgs::msg::UInt8MultiArray>::SharedPtr publisher1_;
    rclcpp::Publisher<std_msgs::msg::UInt8MultiArray>::SharedPtr publisher2_;
    rclcpp::TimerBase::SharedPtr timer1_;
    rclcpp::TimerBase::SharedPtr timer2_;
    rclcpp::TimerBase::SharedPtr status_timer_;
    
    // Subscriber components
    rclcpp::Subscription<std_msgs::msg::UInt8MultiArray>::SharedPtr subscription1_;
    rclcpp::Subscription<std_msgs::msg::UInt8MultiArray>::SharedPtr subscription2_;
    
    // Statistics for subscriber
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point last_status_time_;
    size_t count1_last_status_;
    size_t count2_last_status_;
    double latency_sum1_;
    double latency_sum2_;
    uint32_t latency_count1_;
    uint32_t latency_count2_;
    double latency_sum1_last_second_;
    double latency_sum2_last_second_;
    uint32_t latency_count1_last_second_;
    uint32_t latency_count2_last_second_;
    size_t payload_size1_;
    size_t payload_size2_;
    uint32_t first_msg_id1_;
    uint32_t first_msg_id2_;
    uint32_t last_msg_id1_;
    uint32_t last_msg_id2_;
    uint32_t missed_events1_;
    uint32_t missed_events2_;
    bool first_msg1_;
    bool first_msg2_;
    
    std_msgs::msg::UInt8MultiArray create_message(size_t payload, uint8_t fill_byte, uint32_t msg_id) {
        auto msg = std_msgs::msg::UInt8MultiArray();
        msg.data.resize(payload, fill_byte);
        
        // Set message ID
        std::memcpy(msg.data.data(), &msg_id, sizeof(uint32_t));
        
        // Set timestamp
        auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        if (payload >= sizeof(uint32_t) + sizeof(int64_t)) {
            std::memcpy(msg.data.data() + sizeof(uint32_t), &timestamp, sizeof(int64_t));
        }
        
        return msg;
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
    
    void setup_dual_publisher() {
        publisher1_ = this->create_publisher<std_msgs::msg::UInt8MultiArray>(topic1_name_, 10);
        publisher2_ = this->create_publisher<std_msgs::msg::UInt8MultiArray>(topic2_name_, 10);
        
        auto period1 = std::chrono::milliseconds(static_cast<int>(1000.0 / rate1_));
        auto period2 = std::chrono::milliseconds(static_cast<int>(1000.0 / rate2_));
        
        timer1_ = this->create_wall_timer(period1, std::bind(&DualPubSubNode::publish_topic1, this));
        timer2_ = this->create_wall_timer(period2, std::bind(&DualPubSubNode::publish_topic2, this));
        
        // Status timer
        status_timer_ = this->create_wall_timer(std::chrono::seconds(1), 
                                               std::bind(&DualPubSubNode::print_publisher_status, this));
        
        start_time_ = std::chrono::steady_clock::now();
        last_status_time_ = start_time_;
        count1_last_status_ = 0;
        count2_last_status_ = 0;
        
        RCLCPP_INFO(this->get_logger(), "Dual publisher: %s (%.1f Hz, %zu bytes), %s (%.1f Hz, %zu bytes)",
                    topic1_name_.c_str(), rate1_, payload1_, topic2_name_.c_str(), rate2_, payload2_);
        
        if (duration_ > 0.0) {
            auto duration_timer = this->create_wall_timer(
                std::chrono::milliseconds(static_cast<int>(duration_ * 1000)),
                std::bind(&DualPubSubNode::stop_publishing, this));
        }
    }
    
    void setup_parallel_publisher() {
        // For parallel_pub mode, we still use the same approach but with separate timers
        // The difference is conceptual - in parallel_pub we emphasize concurrent execution
        setup_dual_publisher();
        RCLCPP_INFO(this->get_logger(), "Parallel publisher mode enabled");
    }
    
    void setup_dual_subscriber() {
        subscription1_ = this->create_subscription<std_msgs::msg::UInt8MultiArray>(
            topic1_name_, 10, std::bind(&DualPubSubNode::subscription1_callback, this, _1));
        subscription2_ = this->create_subscription<std_msgs::msg::UInt8MultiArray>(
            topic2_name_, 10, std::bind(&DualPubSubNode::subscription2_callback, this, _1));
        
        // Status timer
        status_timer_ = this->create_wall_timer(std::chrono::seconds(1), 
                                               std::bind(&DualPubSubNode::print_subscriber_status, this));
        
        start_time_ = std::chrono::steady_clock::now();
        last_status_time_ = start_time_;
        count1_last_status_ = 0;
        count2_last_status_ = 0;
        latency_sum1_ = 0.0;
        latency_sum2_ = 0.0;
        latency_count1_ = 0;
        latency_count2_ = 0;
        latency_sum1_last_second_ = 0.0;
        latency_sum2_last_second_ = 0.0;
        latency_count1_last_second_ = 0;
        latency_count2_last_second_ = 0;
        payload_size1_ = 0;
        payload_size2_ = 0;
        first_msg_id1_ = 0;
        first_msg_id2_ = 0;
        last_msg_id1_ = 0;
        last_msg_id2_ = 0;
        missed_events1_ = 0;
        missed_events2_ = 0;
        first_msg1_ = true;
        first_msg2_ = true;
        
        RCLCPP_INFO(this->get_logger(), "Dual subscriber: listening on %s and %s",
                    topic1_name_.c_str(), topic2_name_.c_str());
        
        if (duration_ > 0.0) {
            auto duration_timer = this->create_wall_timer(
                std::chrono::milliseconds(static_cast<int>(duration_ * 1000)),
                std::bind(&DualPubSubNode::stop_subscribing, this));
        }
    }
    
    void publish_topic1() {
        if (finished_) return;
        
        auto now = std::chrono::steady_clock::now();
        if (duration_ > 0.0) {
            double elapsed = std::chrono::duration<double>(now - start_time_).count();
            if (elapsed >= duration_) {
                stop_publishing();
                return;
            }
        }
        
        auto msg = create_message(payload1_, 0xA1, msg_id1_++);
        publisher1_->publish(msg);
        count1_++;
    }
    
    void publish_topic2() {
        if (finished_) return;
        
        auto now = std::chrono::steady_clock::now();
        if (duration_ > 0.0) {
            double elapsed = std::chrono::duration<double>(now - start_time_).count();
            if (elapsed >= duration_) {
                stop_publishing();
                return;
            }
        }
        
        auto msg = create_message(payload2_, 0xB2, msg_id2_++);
        publisher2_->publish(msg);
        count2_++;
    }
    
    void stop_publishing() {
        if (!finished_) {
            finished_ = true;
            if (timer1_) timer1_->cancel();
            if (timer2_) timer2_->cancel();
            if (status_timer_) status_timer_->cancel();
            
            RCLCPP_INFO(this->get_logger(), "Published %zu messages to %s (%.1f Hz, %zu bytes) and %zu messages to %s (%.1f Hz, %zu bytes)",
                        count1_.load(), topic1_name_.c_str(), rate1_, payload1_, 
                        count2_.load(), topic2_name_.c_str(), rate2_, payload2_);
            
            rclcpp::shutdown();
        }
    }
    
    void stop_subscribing() {
        if (!finished_) {
            finished_ = true;
            if (status_timer_) status_timer_->cancel();
            
            RCLCPP_INFO(this->get_logger(), "Received %zu messages from %s and %zu messages from %s",
                        count1_.load(), topic1_name_.c_str(), count2_.load(), topic2_name_.c_str());
            
            rclcpp::shutdown();
        }
    }
    
    void print_publisher_status() {
        if (finished_) return;
        
        auto now = std::chrono::steady_clock::now();
        auto time_since_status = std::chrono::duration<double>(now - last_status_time_).count();
        
        double current_rate1 = (count1_ - count1_last_status_) / time_since_status;
        double current_rate2 = (count2_ - count2_last_status_) / time_since_status;
        
        RCLCPP_INFO(this->get_logger(), "Publishing: %s %zu msgs (%.1f Hz), %s %zu msgs (%.1f Hz)",
                   topic1_name_.c_str(), count1_.load(), current_rate1,
                   topic2_name_.c_str(), count2_.load(), current_rate2);
        
        count1_last_status_ = count1_;
        count2_last_status_ = count2_;
        last_status_time_ = now;
    }
    
    void print_subscriber_status() {
        if (finished_) return;
        
        auto now = std::chrono::steady_clock::now();
        auto time_since_last_display = std::chrono::duration<double>(now - last_status_time_).count();
        
        double rate1 = (count1_ - count1_last_status_) / time_since_last_display;
        double rate2 = (count2_ - count2_last_status_) / time_since_last_display;
        
        double avg_latency1 = std::numeric_limits<double>::quiet_NaN();
        double avg_latency2 = std::numeric_limits<double>::quiet_NaN();
        uint32_t msgs_with_latency1 = latency_count1_ - latency_count1_last_second_;
        uint32_t msgs_with_latency2 = latency_count2_ - latency_count2_last_second_;
        
        if (msgs_with_latency1 > 0) {
            avg_latency1 = (latency_sum1_ - latency_sum1_last_second_) / msgs_with_latency1;
        }
        if (msgs_with_latency2 > 0) {
            avg_latency2 = (latency_sum2_ - latency_sum2_last_second_) / msgs_with_latency2;
        }
        
        double loss_rate1, loss_rate2;
        if (count1_last_status_ == count1_) {
            loss_rate1 = 100.0;
        } else {
            uint32_t total_expected1 = (first_msg1_) ? 0 : last_msg_id1_ - first_msg_id1_;
            loss_rate1 = (total_expected1 == 0) ? 0.0 : static_cast<double>(missed_events1_) / total_expected1 * 100.0;
        }
        
        if (count2_last_status_ == count2_) {
            loss_rate2 = 100.0;
        } else {
            uint32_t total_expected2 = (first_msg2_) ? 0 : last_msg_id2_ - first_msg_id2_;
            loss_rate2 = (total_expected2 == 0) ? 0.0 : static_cast<double>(missed_events2_) / total_expected2 * 100.0;
        }
        
        std::cout << topic1_name_ << ": " << format_bytes(payload_size1_) << ", " << std::fixed << std::setprecision(1) << rate1 << " Hz, "
                  << std::setprecision(2) << avg_latency1 << " ms, "
                  << "loss: " << std::setprecision(2) << loss_rate1 << "%, "
                  << topic2_name_ << ": " << format_bytes(payload_size2_) << ", " << std::setprecision(1) << rate2 << " Hz, "
                  << std::setprecision(2) << avg_latency2 << " ms, "
                  << "loss: " << std::setprecision(2) << loss_rate2 << "%" << std::endl;
        
        count1_last_status_ = count1_;
        count2_last_status_ = count2_;
        latency_sum1_last_second_ = latency_sum1_;
        latency_sum2_last_second_ = latency_sum2_;
        latency_count1_last_second_ = latency_count1_;
        latency_count2_last_second_ = latency_count2_;
        last_status_time_ = now;
    }
    
    void subscription1_callback(const std_msgs::msg::UInt8MultiArray::SharedPtr msg) {
        count1_++;
        payload_size1_ = msg->data.size();
        
        if (msg->data.size() >= sizeof(uint32_t)) {
            uint32_t msg_id;
            std::memcpy(&msg_id, msg->data.data(), sizeof(uint32_t));
            if (first_msg1_) {
                first_msg_id1_ = msg_id;
                last_msg_id1_ = msg_id;
                first_msg1_ = false;
            } else {
                if (msg_id > last_msg_id1_ + 1) {
                    missed_events1_++;
                }
                last_msg_id1_ = msg_id;
            }
        }
        
        if (msg->data.size() >= sizeof(uint32_t) + sizeof(int64_t)) {
            int64_t send_timestamp;
            std::memcpy(&send_timestamp, msg->data.data() + sizeof(uint32_t), sizeof(int64_t));
            auto recv_timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
            double latency_ms = (recv_timestamp - send_timestamp) / 1e6;
            latency_sum1_ += latency_ms;
            latency_count1_++;
        }
    }
    
    void subscription2_callback(const std_msgs::msg::UInt8MultiArray::SharedPtr msg) {
        count2_++;
        payload_size2_ = msg->data.size();
        
        if (msg->data.size() >= sizeof(uint32_t)) {
            uint32_t msg_id;
            std::memcpy(&msg_id, msg->data.data(), sizeof(uint32_t));
            if (first_msg2_) {
                first_msg_id2_ = msg_id;
                last_msg_id2_ = msg_id;
                first_msg2_ = false;
            } else {
                if (msg_id > last_msg_id2_ + 1) {
                    missed_events2_++;
                }
                last_msg_id2_ = msg_id;
            }
        }
        
        if (msg->data.size() >= sizeof(uint32_t) + sizeof(int64_t)) {
            int64_t send_timestamp;
            std::memcpy(&send_timestamp, msg->data.data() + sizeof(uint32_t), sizeof(int64_t));
            auto recv_timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
            double latency_ms = (recv_timestamp - send_timestamp) / 1e6;
            latency_sum2_ += latency_ms;
            latency_count2_++;
        }
    }
};

void print_help(const char *program) {
    std::cout << "Usage: " << program
              << " [--mode pub|sub|parallel_pub] [--topic1 <name>] [--topic2 <name>] [--duration <sec>] [--rate1 <Hz>] [--rate2 <Hz>] [--payload1 <bytes>] [--payload2 <bytes>] [--threads <count>] [--help]\n";
}

bool parse_args(int argc, char *argv[], std::string &mode, std::string &topic1_name, std::string &topic2_name,
                double &duration, double &rate1, double &rate2, std::size_t &payload1, std::size_t &payload2, int &num_threads) {
    mode = "sub";
    topic1_name = "topic_1";
    topic2_name = "topic_2";
    duration = 3.0;
    rate1 = 1.0;
    rate2 = 2.0;
    payload1 = 20;
    payload2 = 40;
    num_threads = 1;

    const struct option long_options[] = {
        {"mode", required_argument, nullptr, 'm'},
        {"topic1", required_argument, nullptr, '1'},
        {"topic2", required_argument, nullptr, '2'},
        {"duration", required_argument, nullptr, 'd'},
        {"rate1", required_argument, nullptr, 'r'},
        {"rate2", required_argument, nullptr, 'R'},
        {"payload1", required_argument, nullptr, 'p'},
        {"payload2", required_argument, nullptr, 'P'},
        {"threads", required_argument, nullptr, 't'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "m:1:2:d:r:R:p:P:t:h", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'm':
                mode = optarg;
                break;
            case '1':
                topic1_name = optarg;
                break;
            case '2':
                topic2_name = optarg;
                break;
            case 'd':
                duration = std::stod(optarg);
                break;
            case 'r':
                rate1 = std::stod(optarg);
                break;
            case 'R':
                rate2 = std::stod(optarg);
                break;
            case 'p':
                payload1 = static_cast<std::size_t>(std::stoul(optarg));
                break;
            case 'P':
                payload2 = static_cast<std::size_t>(std::stoul(optarg));
                break;
            case 't':
                num_threads = std::atoi(optarg);
                if (num_threads <= 0) num_threads = 1;
                break;
            case 'h':
                print_help(argv[0]);
                return false;
            default:
                std::cerr << "Unknown arg\n";
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

int main(int argc, char *argv[]) {
    std::string mode, topic1_name, topic2_name;
    double duration, rate1, rate2;
    std::size_t payload1, payload2;
    int num_threads;

    if (!parse_args(argc, argv, mode, topic1_name, topic2_name, duration, rate1, rate2, payload1, payload2, num_threads)) {
        return 1;
    }

    rclcpp::init(argc, argv);

    auto node = std::make_shared<DualPubSubNode>(mode, topic1_name, topic2_name, duration, rate1, rate2, payload1, payload2);

    if (num_threads <= 1) {
        std::cout << "Using SingleThreadedExecutor (1 thread)" << std::endl;
        rclcpp::executors::SingleThreadedExecutor executor;
        executor.add_node(node);
        executor.spin();
    } else {
        std::cout << "Using MultiThreadedExecutor (" << num_threads << " threads)" << std::endl;
        rclcpp::executors::MultiThreadedExecutor executor(rclcpp::ExecutorOptions(), num_threads);
        executor.add_node(node);
        executor.spin();
    }

    rclcpp::shutdown();
    return 0;
}