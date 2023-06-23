#include "rclcpp/rclcpp.hpp"
#include "yaml-cpp/yaml.h"
#include <regex>
#include <fstream>

#define GREEN "\033[1;32;48m"
#define YELLOW "\033[1;33;48m"
#define RED "\033[1;31;48m"
#define COLOR_END "\033[1;37;0m"

class RosbagCheckerLiveNode: public rclcpp::Node 
{
public:
    RosbagCheckerLiveNode(): Node("rosbag_checker_live")
    {
        // parameters
        RCLCPP_INFO(this->get_logger(), "parameters");
        this->declare_parameter("topic_list", "");
        this->declare_parameter("update_interval", 1000);
        this->declare_parameter("output_file", "");
        output_file_ = this->get_parameter("output_file").as_string();

        // set qos_settings to least stringent settings so it can receive all messages
        qos_settings_.reliability(RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT);
        qos_settings_.durability(RMW_QOS_POLICY_DURABILITY_VOLATILE);
        // qos_settings_.deadline(RMW_QOS_DEADLINE_DEFAULT); // this is called "default" so probably fine to not set manually
        qos_settings_.liveliness(RMW_QOS_POLICY_LIVELINESS_AUTOMATIC);
        // qos_settings_.liveliness_lease_duration(RMW_QOS_LIVELINESS_LEASE_DURATION_DEFAULT); // this is called "default" so probably fine to not set manually

        // intialize topics to 0 msgs to make print_results work
        RCLCPP_INFO(this->get_logger(), "intialize topics to 0 msgs to make print_results work");
        auto yaml_file_loc = this->get_parameter("topic_list").as_string();
        topics_to_rate_ = parse_yaml(yaml_file_loc);
        for (auto pair : topics_to_rate_) {
            auto topic_name = pair.first;
            msg_count_.insert({topic_name, 0});
        }

        // set up print_results functionality
        RCLCPP_INFO(this->get_logger(), "set up print_results functionality");
        update_interval_ms_ = this->get_parameter("update_interval").as_int();
        elapsed_time_ms_ = 0;
        timer_ = this->create_wall_timer(std::chrono::milliseconds(update_interval_ms_), std::bind(&RosbagCheckerLiveNode::print_results, this));

        // initialize subscribers
        RCLCPP_INFO(this->get_logger(), "initialize subscribers");
        std::shared_lock lock(topics_to_rate_lock_); // No locking necessary before here because multi-threading has not began yet
        for (auto pair : topics_to_rate_) { 
            auto topic_name = pair.first;
            threads_.push_back(std::thread(std::bind(&RosbagCheckerLiveNode::init_subscriber, this, topic_name)));
        }

        RCLCPP_INFO(this->get_logger(), "Rosbag Checker Live Node Initialized!");
    }


private:  
    std::map<std::string, std::vector<double>> topics_to_rate_;
    std::map<std::string, int> msg_count_;
    rclcpp::TimerBase::SharedPtr timer_;
    int elapsed_time_ms_;
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
    // int test_duration_ = 59999;
    int callback_count_ = 0;
    std::mutex callback_count_mutex_;
    

    void print_results(){
        elapsed_time_ms_ += update_interval_ms_;
        if (test_duration_ != 0){ // used for debugging
            elapsed_time_ms_ = test_duration_;
        }

        // RCLCPP_INFO(this->get_logger(), "Printing latest results...");
        
        // TODO: print latest results
        std::stringstream output_stream;
        std::shared_lock lock_msg_count(msg_count_lock_);
        for(auto pair : msg_count_){
            auto topic = pair.first;
            auto num_msgs = pair.second;
            auto color_to_use = GREEN;
            double min_rate;
            double max_rate;
            {
                std::shared_lock lock(topics_to_rate_lock_);
                // RCLCPP_INFO(this->get_logger(), "Getting min rate");
                min_rate = topics_to_rate_[topic][0];
                // RCLCPP_INFO(this->get_logger(), "Getting max rate");
                max_rate = topics_to_rate_[topic][1];
            }
            // RCLCPP_INFO(this->get_logger(), "Determining which color to use");
            if (num_msgs == 0){
                color_to_use = RED;
            }
            else if ((num_msgs*1000.0)/elapsed_time_ms_ > max_rate || (num_msgs*1000.0)/elapsed_time_ms_ < min_rate) {
                color_to_use = YELLOW;
            }
            // RCLCPP_INFO(this->get_logger(), "Pushing results to output_stream");
            output_stream << color_to_use << "Statistics for topic " << topic << "\n" << "Message count = " << num_msgs << ", Message frequency = " << ((double)1000*num_msgs)/elapsed_time_ms_ << COLOR_END << "\n\n";
        }

        RCLCPP_INFO(this->get_logger(), "Latest results:\n%s", output_stream.str().c_str());

        if (output_file_ != ""){
            std::ofstream out(output_file_);
            out << output_stream.str();
            out.close();
        }
    }

    void callback_subscription(std::string topic_name){ 
        callback_count_mutex_.lock();
        if (++callback_count_ > 1){
            RCLCPP_INFO(this->get_logger(), "Callback parallelization success! Currently, %d callbacks are executing", callback_count_);
        }
        else {
            // RCLCPP_INFO(this->get_logger(), "Currently no callback parallelization. %d callbacks are executing", callback_count_);
        }
        callback_count_mutex_.unlock();
        // RCLCPP_INFO(this->get_logger(), "Incrementing msg_count[%s]", topic_name.c_str());
        std::unique_lock lock(msg_count_lock_); // TODO: I may be able to make this lock shared if I can guarantee no one is accessing the same element?
        msg_count_[topic_name]++;
        // RCLCPP_INFO(this->get_logger(), "Msg count incremented!");
        callback_count_mutex_.lock();
        callback_count_--;
        callback_count_mutex_.unlock();
    }

    void init_subscriber(std::string topic_name){ // **CRITICAL SECTION**
        std::regex re(topic_name);
        while(true){
            std::vector<std::string> topic_type;
            for (auto name_type_pair : rclcpp::Node::get_topic_names_and_types()){
                {
                    std::shared_lock lock(topics_to_subscribers_lock_);
                    if (name_type_pair.second.size() == 0 || topics_to_subscribers_.count(name_type_pair.first)){ // continue if topic has no publishers yet or we already have a subscriber for this topic
                        continue;
                    }
                }

                if (std::regex_match(name_type_pair.first, re)){

                    RCLCPP_INFO(this->get_logger(), "Message type determined"); 
                    RCLCPP_INFO(this->get_logger(), "\nSUBSCRIBING TO TOPIC %s\n", name_type_pair.first.c_str());

                    rclcpp::SubscriptionOptions subscription_options;
                    subscription_options.event_callbacks.incompatible_qos_callback = std::bind(&RosbagCheckerLiveNode::incompatible_qos_callback, this);
                    auto new_callback_group = this->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
                    subscription_options.callback_group = new_callback_group;
                    subscription_options_list_.push_back(subscription_options);
                    auto new_sub = this->create_generic_subscription(name_type_pair.first, name_type_pair.second[0], qos_settings_, std::bind(&RosbagCheckerLiveNode::callback_subscription, this, name_type_pair.first), subscription_options);
                    
                    { // place inside block so that lock is released immediately after insertion
                        std::unique_lock lock(topics_to_subscribers_lock_); // TODO: lock!
                        topics_to_subscribers_.insert({name_type_pair.first, new_sub});
                    }

                    if (topic_name.compare(name_type_pair.first) == 0){ // if full match, then this is not a regex, and we can stop looping
                        RCLCPP_INFO(this->get_logger(), "Returning because not regex, known because %s == %s", topic_name.c_str(), name_type_pair.first.c_str()); 
                        return;
                    }
                    else { // if regex, make sure to add actual topic name and remove regex from topics to print (because regex will always have 0 msgs)
                        {
                            std::unique_lock lock(msg_count_lock_);
                            msg_count_.insert({name_type_pair.first, 0});
                            msg_count_.erase(topic_name);
                        }
                        std::unique_lock lock(topics_to_rate_lock_);
                        topics_to_rate_.insert({name_type_pair.first, std::vector<double>{-1, 1000}});
                    }
                }
            }
        }
    }

    std::map<std::string, std::vector<double>> parse_yaml(std::string yaml_file_loc){
        RCLCPP_INFO(this->get_logger(), "yaml file location: %s", yaml_file_loc.c_str());
        YAML::Node topic_list = YAML::LoadFile(yaml_file_loc);

        const YAML::Node& topics = topic_list["topics"];
        std::map<std::string, std::vector<double>> topics_to_rate;
        for (YAML::const_iterator it = topics.begin(); it != topics.end(); ++it) {
            const YAML::Node& topic = *it;
            auto topic_name = topic["name"].as<std::string>();
            std::vector<double> hz_range;
            try {
                hz_range = topic["hz_range"].as<std::vector<double>>();
            }
            catch(...) {
                hz_range = std::vector<double>({-1, 1000000});
            }
            topics_to_rate.insert({topic_name, hz_range});
        }


        return topics_to_rate;
    }

    void incompatible_qos_callback(){ // this should not happen but adding this for testing (so I can notice and fix a bug if this occurs)
        RCLCPP_ERROR(this->get_logger(), "Incompatible QoS requested!");
    }
};

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<RosbagCheckerLiveNode>();

    rclcpp::executors::MultiThreadedExecutor executor;
    executor.add_node(node);
    executor.spin();

    rclcpp::shutdown();
    return 0;
}