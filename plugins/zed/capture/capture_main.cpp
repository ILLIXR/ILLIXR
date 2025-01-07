#include "capture.hpp"

#include "cxxopts.hpp"
#include "files.hpp"

bool ends_with(const std::string& word, const std::string& end) {
    auto pos = word.rfind(end);
    if (pos != std::string::npos && (word.substr(pos, word.size()) == end))
        return true;
    return false;
}

int main(int argc, const char* argv[]) {
    cxxopts::Options options("zed_capture", "main");
    options.show_positional_help();
    options.add_options()("d,duration", "The duration to run for in seconds", cxxopts::value<int>()->default_value("10"))
        ("f,fps", "Frames per second", cxxopts::value<int>()->default_value("30"))
        ("wc,world_coordinates", "The origin of the world coordinate system in relation to the camera. Must be 7 comma separated values x, y, z, w, wx, wy, wz.", cxxopts::value<std::string>()->default_value("0.,0.,0.,1.,0.,0.,0."))
        ("p,path", "The root path to write the data to. Default is current working directory.", cxxopts::value<std::string>()->default_value("."))
        ("h,help", "Produce help message");
    auto opts = options.parse(argc, argv);
    if (opts.count("help")) {
        std::cout << options.help() << std::endl;
        return EXIT_SUCCESS;
    }
    const int fps = opts["fps"].as<int>();
    const int duration = opts["duration"].as<int>();
    const std::string wcs = opts["world_coordinates"].as<std::string>();
    std::string root = opts["path"].as<std::string>();
    if (!ends_with(root, "/"))
        root += "/";

    std::stringstream  iss(wcs);
    std::string        token;
    std::vector<float> ip;
    while (!iss.eof() && std::getline(iss, token, ',')) {
        ip.emplace_back(std::stof(token));
    }
    ILLIXR::data_format::pose_data wcs_origin({ip[0], ip[1], ip[2]},
                                              {ip[3], ip[4], ip[5], ip[6]});
    const std::string sub_path = "fps" + std::to_string(fps) + "_dur" + std::to_string(duration) + "/";
    ILLIXR::zed_capture::files* fls = ILLIXR::zed_capture::files::getInstance(root, sub_path);


    std::shared_ptr<ILLIXR::zed_capture::capture> cap = std::make_shared<ILLIXR::zed_capture::capture>(fps, wcs_origin);

    for (int i = 3; i >0; i--) {
        std::cout << "Starting capture in " << i << " seconds" << "\t\r" << std::flush;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "Capture starting" << std::endl;
    int current = 0;
    while (current < fps * duration) {
        std::cout << "\t\r" << std::flush;
        std::cout << "Capturing frame " << current + 1 << " / " << fps * duration;
        current += cap->get_data();
    }
    std::cout << std::endl << "Capture complete. Files written to " << root << sub_path << std::endl;
    delete fls;
    return EXIT_SUCCESS;
}