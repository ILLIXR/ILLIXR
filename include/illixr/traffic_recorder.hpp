#pragma once

#ifdef RECORD_NETWORK_TRAFFIC

#    include <fstream>
#    include <string>
#    include <vector>

#    define TRAFFIC_BUFFER_SIZE 50

namespace ILLIXR {

class traffic_recorder {
public:
    traffic_recorder() = delete;
    traffic_recorder(const std::string& file_name)
        : traffic_record_{file_name} {
        if (!traffic_record_.is_open())
            throw std::runtime_error("Cannot open record file: " + file_name);
    }
    
    ~traffic_recorder() {
        flush();
    }
    
    void record(const std::string& record) {
        {
            std::lock_guard<std::mutex> lock{traffic_mutex_};
            traffic_list_.push_back(record + "\n");
        }
        if(traffic_list_.size() >= TRAFFIC_BUFFER_SIZE)
            flush();
    }

    void flush() {
        std::lock_guard<std::mutex> lock{traffic_mutex_};
        for(const auto& st : traffic_list_)
            traffic_record_ << st;
        traffic_record_.flush();
        traffic_list_.clear();
    }

private:
    std::ofstream traffic_record_;
    std::vector<std::string> traffic_list_;
    std::mutex traffic_mutex_;

};
}

#endif
