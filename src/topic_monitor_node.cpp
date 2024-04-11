#include <rclcpp/rclcpp.hpp>
#include <rclcpp_components/register_node_macro.hpp>
#include <yaml-cpp/yaml.h>
#include <regex>
#include <fstream>
#include <chrono>

#define GREEN "\033[1;32;48m"
#define YELLOW "\033[1;33;48m"
#define RED "\033[1;31;48m"
#define COLOR_END "\033[1;37;0m"

using namespace std::chrono_literals;

class RosbagCheckerLiveNode : public rclcpp::Node
{
public:
    RosbagCheckerLiveNode(const rclcpp::NodeOptions & options) : Node("rosbag_checker_live", options)
    {
        // parameters
        RCLCPP_INFO(this->get_logger(), "parameters");
        this->declare_parameter("topic_list", "");
        this->declare_parameter("topics", "");
        this->declare_parameter("update_interval", 1000);
        this->declare_parameter("output_file", "");
        this->declare_parameter("default_frequency_requirements", std::vector<double>({-1.0, std::numeric_limits<double>::max()}));
        this->declare_parameter("buffer_size", -1);
        this->declare_parameter("verbose", 1);
        this->declare_parameter("window_size", 50);

        output_file_ = this->get_parameter("output_file").as_string();
        frequency_requirements_ = this->get_parameter("default_frequency_requirements").as_double_array();
        buffer_size_ = this->get_parameter("buffer_size").as_int();
        verbose_ = this->get_parameter("verbose").as_int();
        window_size_ = get_parameter("window_size").as_int();

        // set qos_settings to most permissive settings so it can receive all messages
        qos_settings_.reliability(RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT);
        qos_settings_.durability(RMW_QOS_POLICY_DURABILITY_VOLATILE);
        qos_settings_.liveliness(RMW_QOS_POLICY_LIVELINESS_AUTOMATIC);
        qos_settings_.keep_last(10);

        // intialize topics to 0 msgs to make print_results work
        RCLCPP_INFO(this->get_logger(), "intialize topics to 0 msgs to make print_results work");
        auto yaml_file_loc = this->get_parameter("topic_list").as_string();
        auto input_topics = this->get_parameter("topics").as_string();
        if (!yaml_file_loc.empty())
        {
            parse_yaml(yaml_file_loc);
        }
        else
        {
            RCLCPP_ERROR(this->get_logger(), "No topics to monitor specified");
        }

        for (auto pair : types_)
        {
            auto topic_name = pair.first;
            rclcpp::SubscriptionOptions subscription_options;
            subscription_options.topic_stats_options.state = rclcpp::TopicStatisticsState::Enable;
            subscription_options.topic_stats_options.publish_topic = "/stats";
            subscription_options.topic_stats_options.publish_period = 1s;
            auto new_callback_group = this->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
            subscription_options.callback_group = new_callback_group;
            subscription_options_list_.push_back(subscription_options);
            topics_to_subscribers_.insert({pair.first, create_generic_subscription(pair.first, pair.second, qos_settings_, std::bind(&RosbagCheckerLiveNode::callback_subscription, this, pair.first), subscription_options)});
        }

        // initialize subscribers
        std::shared_lock lock(topics_to_rate_lock_); // No locking necessary before here because multi-threading has not began yet
        for (auto pair : topics_to_rate_)
        {
            auto topic_name = pair.first;
        }

        RCLCPP_INFO(this->get_logger(), "Rosbag Checker Live Node Initialized!");
    }

private:
    std::map<std::string, std::vector<double>> topics_to_rate_;
    std::map<std::string, int> msg_count_;
    rclcpp::TimerBase::SharedPtr timer_;
    std::map<std::string, std::deque<std::chrono::high_resolution_clock::time_point>> receive_times_;
    std::map<std::string, std::chrono::high_resolution_clock::time_point> last_print_;
    std::map<std::string, std::string> types_;
    int update_interval_ms_;
    std::map<std::string, rclcpp::GenericSubscription::SharedPtr> topics_to_subscribers_;
    std::vector<std::thread> threads_;
    rclcpp::QoS qos_settings_ = rclcpp::QoS(10); // have to initialize here?
    std::vector<rclcpp::SubscriptionOptions> subscription_options_list_;
    std::shared_mutex topics_to_rate_lock_;
    std::shared_mutex msg_count_lock_;
    std::shared_mutex topics_to_subscribers_lock_;
    std::string output_file_;
    int test_duration_ = 0;
    int callback_count_ = 0;
    std::mutex callback_count_mutex_;
    std::vector<double> frequency_requirements_;
    bool verbose_;
    int buffer_size_;
    size_t window_size_;

    void callback_subscription(std::string topic_name)
    {
        // callback_count_mutex_.lock();
        // if (++callback_count_ > 1){
        //     RCLCPP_INFO(this->get_logger(), "Callback parallelization success! Currently, %d callbacks are executing", callback_count_);
        // }
        // callback_count_mutex_.unlock();
        // RCLCPP_INFO(this->get_logger(), "Incrementing msg_count[%s]", topic_name.c_str());
        msg_count_[topic_name]++;
        auto & queue = receive_times_[topic_name];
        queue.push_front(std::chrono::high_resolution_clock::now());

        while (queue.size() > window_size_) {
            queue.pop_back();
        }

        if (std::chrono::high_resolution_clock::now() - last_print_[topic_name] < 1s) {
            return;
        }

        std::stringstream ss;

        auto color_to_use = GREEN;
        double min_rate;
        double max_rate;

        min_rate = topics_to_rate_[topic_name][0];
        max_rate = topics_to_rate_[topic_name][1];

        auto rate = static_cast<double>(queue.size() - 1) / to_ns(queue.front() - queue.back()) * 1e9;

        if (msg_count_[topic_name] <= 0)
        {
            color_to_use = RED;
        }
        else if (rate > max_rate || rate < min_rate)
        {
            color_to_use = YELLOW;
        }
        ss << color_to_use << topic_name << ": N = " << msg_count_[topic_name] << ": n = " << queue.size() << ", f = " << rate << COLOR_END;
        RCLCPP_INFO_STREAM(get_logger(), ss.str());
        // RCLCPP_INFO(this->get_logger(), "Msg count incremented!");
        // callback_count_mutex_.lock();
        // callback_count_--;
        // callback_count_mutex_.unlock();
        last_print_[topic_name] = std::chrono::high_resolution_clock::now();
    }

    void parse_yaml(std::string yaml_file_loc)
    {
        RCLCPP_INFO(this->get_logger(), "yaml file location: %s", yaml_file_loc.c_str());
        YAML::Node topic_list = YAML::LoadFile(yaml_file_loc);

        const YAML::Node &topics = topic_list["topics"];
        for (YAML::const_iterator it = topics.begin(); it != topics.end(); ++it)
        {
            const YAML::Node &topic = *it;
            auto topic_name = topic["name"].as<std::string>();
            auto hz_range = topic["hz_range"].as<std::vector<double>>();
            topics_to_rate_.insert({topic_name, hz_range});
            types_.insert({topic_name, topic["type"].as<std::string>()});
            receive_times_.insert({topic_name, {}});
            last_print_.insert({topic_name, std::chrono::high_resolution_clock::now()});
        }

    }

    void incompatible_qos_callback()
    { // this should not happen but adding this for testing (so I can notice and fix a bug if this occurs)
        RCLCPP_ERROR(this->get_logger(), "Incompatible QoS requested!");
    }

    uint64_t to_ns(std::chrono::high_resolution_clock::duration  d) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
    }
    
    uint64_t to_ns(std::chrono::high_resolution_clock::time_point  tp) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count();
    }
};

RCLCPP_COMPONENTS_REGISTER_NODE(RosbagCheckerLiveNode)