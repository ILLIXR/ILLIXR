#ifndef CONCURRENT_UTILS_HH
#define CONCURRENT_UTILS_HH

#include <sched.h>
#include <atomic>
#include <future>
#include <unistd.h>
#include <signal.h>
#include <cerrno>
#include <cstring>
#include <exception>
#include <sys/wait.h>

class concurrent_thread {
public:
	void start() {
		// note that &concurrent::run calls run in the derived class
		std::async(std::launch::async, &concurrent_thread::run, this);
	}
	virtual void run() = 0;
};

class concurrent_process {
public:
	void start() {
		// 0
		// 	| CLONE_FILES
		// 	| CLONE_FS
		// 	| CLONE_VM
			// leave unset: CLONE_PID, CLONE_SIGHAND
		pid_t pid = fork();
		if (pid == 0) {
			// child process
			this->run();
		} else if (pid > 0) {
			_m_pid = pid;
			// parent process
		} else {
			// parent process, no child
			throw std::runtime_error{strerror(errno)};
		}
	}
	void terminate() {
		if (!kill(_m_pid, SIGTERM)) {
			perror("kill -SIGTERM");
		}
		int status;
		if (waitpid(_m_pid, &status, WNOHANG) != _m_pid) {
			perror("waitpid");
		}
		if (!kill(_m_pid, SIGKILL)) {
			perror("kill -SIGKILL");
		}
	}
	virtual void run() = 0;
	~concurrent_process() {
		if (_m_pid) {
		}
	}
private:
	pid_t _m_pid {0};
};

template <typename concurrent_st>
class concurrent_loop : public virtual concurrent_st {
public:
	virtual void run() {
		while (!_m_terminate.load()) {
			main_loop();
		}
	}
	virtual void main_loop() = 0;
	void terminate() { _m_terminate.store(true); }
private:
	std::atomic<bool> _m_terminate {false};
};

template <typename T>
class double_buffer {
public:
	/* I use swap so that we only ever allocate two buffers. The
	   producer gets its old one back. */
	void swap() {
		_m_which ^= 1;
	}
	T& get_incomplete() {
		return _m_buffers[_m_which ^ 1];
	}
	const T& get_complete() const {
		return _m_buffers[_m_which];
	}
private:
	T _m_buffers[2];
	std::atomic<int> _m_which;
};

#endif
