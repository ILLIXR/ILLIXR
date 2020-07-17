#include <chrono>
#include "phonebook.hpp"
#include <string>
#include <fstream>

namespace ILLIXR{

	/**
	 * @brief a utility for logging to start and end times to a file
	 */
	class start_end_logger
	{
	private:
		std::string component_name;
		std::ofstream log_file;
		enum class start_end_state{
			started,
			ended,
			bad
		};
		start_end_state log_state;
		std::chrono::time_point<std::chrono::system_clock> init_time;
	public:
		/**
		 * Log apperas in `log/$component_name`.
		 */
		start_end_logger(std::string component_name){
			log_file.open("log/" + component_name);
			if (log_file) {
				log_state = start_end_state::ended;
				log_file << "logger.hpp is DEPRECATED. See logging.hpp" << std::endl;
			} else {
				log_state = start_end_state::bad;
			}
			init_time = std::chrono::high_resolution_clock::now();
		}
		~start_end_logger(){
			log_file.close();
		}
		int log_start(std::chrono::time_point<std::chrono::system_clock> log_time){
			// check status
			if (log_state == start_end_state::bad) return -1;
			if (log_state == start_end_state::started){
				log_state = start_end_state::bad;
				log_file << "bad logging state, logging terminated" << std::endl;
				return -1;
			}

			log_state = start_end_state::started;
			log_file << std::chrono::duration_cast<std::chrono::milliseconds>(log_time - init_time).count() << ",";

			return 0;
		}
		int log_end(std::chrono::time_point<std::chrono::system_clock> log_time){
			// check status
			if (log_state == start_end_state::bad) return -1;
			if (log_state == start_end_state::ended){
				log_state = start_end_state::bad;
				log_file << "bad logging state, logging terminated" << std::endl;
				return -1;
			}

			log_state = start_end_state::ended;
			log_file << std::chrono::duration_cast<std::chrono::milliseconds>(log_time - init_time).count() << std::endl;

			return 0;
		}
	};
}
