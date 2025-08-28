#include <iostream>
#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>
#include <ctime>
#include <csignal>
#include <atomic>
#include <sys/un.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <zmq.h>
#include <zmq.hpp>
#include <memory>


using json = nlohmann::json;

std::atomic<bool> running(true);

struct SensorData {
    std::string sensor_id;
    double value;
    std::string timestamp;
    bool is_valid;
    
    SensorData() : sensor_id(""), value(-1.0), timestamp(""), is_valid(false) {}
};

SensorData sensor_reading;
int write_counter = 0;

// CPU usage calculation variables
struct CpuTimes {
    long user, nice, system, idle, iowait, irq, softirq, steal;
};

CpuTimes prev_cpu_times = {0};

void handle_sigint(int) {
    std::cout << "\n[INFO] SIGINT received. Exiting gracefully..." << std::endl;
    running = false;
}

std::string timestamp() {
    std::time_t now = std::time(nullptr);
    char buf[100];
    std::strftime(buf, sizeof(buf), "%FT%TZ", std::gmtime(&now));
    return std::string(buf);
}

CpuTimes read_cpu_times() {
    std::ifstream file("/proc/stat");
    std::string line;
    CpuTimes times = {0};
    
    if (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string cpu_label;
        ss >> cpu_label >> times.user >> times.nice >> times.system >> times.idle 
           >> times.iowait >> times.irq >> times.softirq >> times.steal;
    }
    
    return times;
}

double calculate_cpu_usage() {
    CpuTimes current = read_cpu_times();
    
    // Calculate differences
    long prev_idle = prev_cpu_times.idle + prev_cpu_times.iowait;
    long curr_idle = current.idle + current.iowait;
    
    long prev_non_idle = prev_cpu_times.user + prev_cpu_times.nice + prev_cpu_times.system + 
                        prev_cpu_times.irq + prev_cpu_times.softirq + prev_cpu_times.steal;
    long curr_non_idle = current.user + current.nice + current.system + 
                        current.irq + current.softirq + current.steal;
    
    long prev_total = prev_idle + prev_non_idle;
    long curr_total = curr_idle + curr_non_idle;
    
    long total_diff = curr_total - prev_total;
    long idle_diff = curr_idle - prev_idle;
    
    double cpu_usage = 0.0;
    if (total_diff > 0) {
        cpu_usage = ((double)(total_diff - idle_diff) / total_diff) * 100.0;
    }
    
    prev_cpu_times = current;  // Update for next calculation
    
    std::cout << "[INFO] CPU usage: " << cpu_usage << "%" << std::endl;
    return cpu_usage;
}

void sensor_thread() {
    std::cout << "[INFO] CPU usage sensor thread started." << std::endl;
    
    // Initialize CPU times
    prev_cpu_times = read_cpu_times();
    std::this_thread::sleep_for(std::chrono::seconds(1)); // Wait for initial reading
    
    while (running) {
        double cpu_usage = calculate_cpu_usage();
        
        sensor_reading.sensor_id = "cpu_usage_01";
        std::this_thread::sleep_for(std::chrono::microseconds(100)); // Deliberate delay
        
        sensor_reading.value = cpu_usage;
        std::this_thread::sleep_for(std::chrono::microseconds(100)); // Deliberate delay
        
        sensor_reading.timestamp = timestamp();
        std::this_thread::sleep_for(std::chrono::microseconds(100)); // Deliberate delay
        
        sensor_reading.is_valid = true;
        write_counter++;

        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Fast updates to increase contention
    }
    std::cout << "[INFO] CPU usage sensor thread exiting." << std::endl;
}

double get_disk_usage_percent(const std::string& path = "/") {
    struct statvfs stat;
    if (statvfs(path.c_str(), &stat) != 0) {
        std::cerr << "[ERROR] Failed to get disk usage for path: " << path << std::endl;
        return -1.0;
    }

    double total = static_cast<double>(stat.f_blocks) * stat.f_frsize;
    double free = static_cast<double>(stat.f_bfree) * stat.f_frsize;
    double used = total - free;
    double percent = (used / total) * 100.0;

    std::cout << "[INFO] Disk usage at '" << path << "': " << percent << "%" << std::endl;
    return percent;
}

void disk_usage_thread() {
    std::cout << "[INFO] Disk usage thread started." << std::endl;
    while (running) {
        double usage = get_disk_usage_percent("/");
    
        sensor_reading.sensor_id = "disk_usage_root";
        std::this_thread::sleep_for(std::chrono::microseconds(150)); // Deliberate delay
        
        sensor_reading.value = usage;
        std::this_thread::sleep_for(std::chrono::microseconds(150)); // Deliberate delay
        
        sensor_reading.timestamp = timestamp();
        std::this_thread::sleep_for(std::chrono::microseconds(150)); // Deliberate delay
        
        sensor_reading.is_valid = true;
        write_counter++;
        
        std::this_thread::sleep_for(std::chrono::milliseconds(75)); // Fast updates to increase contention
    }
    std::cout << "[INFO] Disk usage thread exiting." << std::endl;
}


// ZeroMQ globals for comm thread 
static std::unique_ptr<zmq::context_t> g_ctx;   
static std::unique_ptr<zmq::socket_t>  g_sock; 

void comm_thread() {
    std::cout << "[INFO] Communication thread started." << std::endl;
    int corruption_count = 0;
    int total_reads = 0;
    
    while (running) {
        SensorData current_reading;
        bool data_consistent = true;
        
        current_reading = sensor_reading;
        
        total_reads++;
        
        if (current_reading.is_valid) {
            if ((current_reading.sensor_id == "cpu_usage_01" && (current_reading.value > 100 || current_reading.value < 0)) ||
                (current_reading.sensor_id == "disk_usage_root" && current_reading.value > 100) ||
                current_reading.sensor_id.empty() ||
                current_reading.timestamp.empty()) {
                
                corruption_count++;
                data_consistent = false;
                std::cout << "[ERROR] Data corruption! ID: " << current_reading.sensor_id 
                         << ", Value: " << current_reading.value 
                         << ", Timestamp: " << current_reading.timestamp << std::endl;
            }
            
            // Create and send JSON message
            json message;
            if (current_reading.sensor_id == "cpu_usage_01") {
                message = {
                    {"sensor_id", current_reading.sensor_id},
                    {"timestamp", current_reading.timestamp},
                    {"cpu_usage_percent", current_reading.value},
                    {"data_consistent", data_consistent}
                };
            } else {
                message = {
                    {"sensor_id", current_reading.sensor_id},
                    {"timestamp", current_reading.timestamp},
                    {"disk_usage_percent", current_reading.value},
                    {"data_consistent", data_consistent}
                };
            }
            
            std::string msg = message.dump() + "\n";

            // send/recv via ZeroMQ REQ/REP  
            std::string payload = message.dump();
            try {
                g_sock->send(zmq::buffer(payload), zmq::send_flags::none);
                zmq::message_t reply;
                auto ok = g_sock->recv(reply, zmq::recv_flags::none);
                if (!ok) std::cerr << "[WARN] No reply from processor\n";
            } catch (const std::exception& ex) {
                std::cerr << "[ERROR] ZMQ send/recv failed: " << ex.what() << "\n";
            }
            
            if (total_reads % 50 == 0) {
                double corruption_rate = (double)corruption_count / total_reads * 100.0;
                std::cout << "[STATS] Total reads: " << total_reads 
                         << ", Corruptions: " << corruption_count 
                         << " (" << corruption_rate << "%)" << std::endl;
            }
        }



        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    double final_corruption_rate = total_reads > 0 ? (double)corruption_count / total_reads * 100.0 : 0.0;
    std::cout << "[FINAL STATS] Total reads: " << total_reads 
             << ", Corruptions: " << corruption_count 
             << " (" << final_corruption_rate << "%)" << std::endl;
    std::cout << "[INFO] XXX thread exiting." << std::endl;
}

int main() {
    std::cout << "[INFO] Starting sensor service..." << std::endl;
    std::signal(SIGINT, handle_sigint);

    // Build a communication mechanism with the processor [ADDED]
    const std::string endpoint = "tcp://processor:5555";
    std::cout << "[INFO] Connecting to processor at " << endpoint << std::endl;
    try {
        g_ctx  = std::make_unique<zmq::context_t>(1);
        g_sock = std::make_unique<zmq::socket_t>(*g_ctx, zmq::socket_type::req);
        g_sock->connect(endpoint);
    } catch (const std::exception& ex) {
        std::cerr << "[ERROR] Failed to connect to processor: " << ex.what() << std::endl;
        return 1;
    }
    std::cout << "[INFO] Connection created successfully." << std::endl;


    std::thread t1(sensor_thread);
    std::thread t2(disk_usage_thread);
    std::thread t3(comm_thread);        

    t1.join();
    t2.join();
    running = false;
    t3.join();  

    std::cout << "[INFO] Sensor service stopped." << std::endl;
    return 0;
}
