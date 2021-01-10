#pragma once

/**
 * A miniature port of a subset of the C++ filesystem stdlib specialized for POSIX.
 *
 * I take no joy in supporting antiquated systems.
 */
namespace filesystem {
	class _zero_errno {
	public:
		_zero_errno() {
			/*
			  errno should really be zero on program startup, but for some reason, sometimes it's not.
			  This may have to with our incomplete handling of X11 and OpenGL calls.
			  In any case, errno has to be cleared at program startup, or else innocent calls that dilligently check errno think they caused an errno.
			*/
			if (errno != 0) {
				std::cerr << "For an unknown reason, errno = " << errno << ", " << strerror(errno) << ". I'm clearing it now\n";
				errno = 0;
			}
		}
	};
	static _zero_errno __zero_errno;

	class path {
	public:
		typedef char value_type;
		typedef std::basic_string<value_type> string_type;
		path(const value_type* val_) : val{val_} { replace_all(val, "/", "\\/"); }
		path(string_type&& val_) : val{std::move(val_)} { replace_all(val, "/", "\\/"); }
		path(const string_type& val_) : val{std::move(val_)} { replace_all(val, "/", "\\/"); }
		path operator/(path other_path) const { return {val + "/" + other_path.val, true}; }
		path operator+(path other_path) const { return {val + other_path.val, true}; }
		const char* c_str() const { return val.c_str(); }
		std::string string() const { return val; }
	private:
		path(string_type&& val_, bool) : val{std::move(val_)} {}
		string_type val;
	};

	class filesystem_error : public std::system_error {
	public:
		filesystem_error(const std::string& what_arg, const path& p1, const path& p2, int ec)
			: filesystem_error(what_arg, p1, p2, std::make_error_code(std::errc(ec)))
		{ }
		filesystem_error(const std::string& what_arg, const path& p1, const path& p2, std::error_code ec)
			: system_error{ec, what_arg + " " + p1.string() + " " + p2.string() + ": " + std::to_string(ec.value())}
		{ }
		filesystem_error(const std::string& what_arg, const path& p1, std::error_code ec)
			: system_error{ec, what_arg + " " + p1.string() + ": " + std::to_string(ec.value())}
		{ }
		filesystem_error(const std::string& what_arg, const path& p1, int ec)
			: filesystem_error(what_arg, p1, std::make_error_code(std::errc(ec))) { }
	};

	class directory_iterator;

	class directory_entry {
	private:
		path this_path;
		bool _is_directory;
		bool _exists;
	public:
		directory_entry(const path& this_path_, std::error_code ec) : this_path{this_path_} { refresh(ec); }
		directory_entry(const path& this_path_) : this_path{this_path_} { refresh(); }
		void refresh(std::error_code& ec) {
			assert(errno == 0);
			struct stat buf;
#ifdef FILESYSTEM_DEBUG
			std::cerr << "stat " << this_path.string() << "\n";
#endif
			if (stat(this_path.c_str(), &buf)) {
				ec = std::make_error_code(std::errc(errno));
				errno = 0;
				_is_directory = false;
				_exists = false;
			} else {
				ec.clear();
				_is_directory = S_ISDIR(buf.st_mode);
				_exists = true;
			}
			assert(errno == 0);
		}
		void refresh() {
			std::error_code ec;
			refresh(ec);
			if (ec) {
				if (ec.value() != ENOENT) {
					throw filesystem_error{std::string{"stat"}, this_path, ec};
				}
			}
		}
		path path() const { return this_path; }
		bool is_directory() const { return _is_directory; }
		bool exists() { return _exists; }
	};

	class directory_iterator {
	private:
		std::deque<directory_entry> dir_entries;
	public:
		directory_iterator(const path& dir_path) {
			assert(errno == 0);
#ifdef FILESYSTEM_DEBUG
			std::cerr << "ls " << dir_path.string() << "\n";
#endif
			if (bool_likely(DIR* dir = opendir(dir_path.c_str()))) {
				assert(errno == 0);
				struct dirent* dir_entry = readdir(dir);
				while (bool_likely(dir_entry)) {
					bool skip = strcmp(dir_entry->d_name, ".") == 0 || strcmp(dir_entry->d_name, "..") == 0;
#ifdef FILESYSTEM_DEBUG
					std::cerr << "ls " << dir_path.string() << " -> " << dir_entry->d_name << " skip=" << skip << "\n";
#endif
					if (!skip) {
						dir_entries.emplace_back(dir_path / std::string{dir_entry->d_name});
					}
					assert(errno == 0);
					dir_entry = readdir(dir);
				}
				if (bool_unlikely(errno != 0)) {
					int errno_ = errno; errno = 0;
					throw filesystem_error{std::string{"readdir"}, dir_path, errno_};
				}
				if (bool_unlikely(closedir(dir))) {
					int errno_ = errno; errno = 0;
					throw filesystem_error{std::string{"closedir"}, dir_path, errno_};
				}
			} else {
				int errno_ = errno; errno = 0;
				throw filesystem_error{std::string{"opendir"}, dir_path, errno_};
			}
		}
		directory_iterator() { }
		bool operator==(const directory_iterator& other) {
			if (dir_entries.size() == other.dir_entries.size()) {
				for (auto dir_entry0 = dir_entries.cbegin(), dir_entry1 = other.dir_entries.cbegin();
					 dir_entry0 != dir_entries.cend();
					 ++dir_entry0, ++dir_entry1) {
					if (dir_entry0 != dir_entry1) {
						return false;
					}
				}
				return true;
			} else {
				return false;
			}
		}
		bool operator!=(const directory_iterator& other) { return !(*this == other); }
		typedef directory_entry value_type;
		typedef std::ptrdiff_t difference_type;
		typedef const directory_entry* pointer;
		typedef const directory_entry& reference;
		typedef std::input_iterator_tag iterator_category;
		reference operator*() { assert(!dir_entries.empty()); return dir_entries.back(); }
		reference operator->() { return **this; }
		directory_iterator& operator++() {
			assert(!dir_entries.empty());
			dir_entries.pop_back();
			return *this;
		}
		directory_iterator operator++(int) {
			auto ret = *this;
			++*this;
			return ret;
		}
	};

	[[maybe_unused]] static std::deque<directory_entry> post_order(const path& this_path) {
		std::deque<directory_entry> ret;
		std::deque<directory_entry> stack;
		directory_entry this_dir_ent {this_path};
		if (this_dir_ent.exists()) {
			stack.push_back(this_dir_ent);
		}
		while (!stack.empty()) {
			directory_entry current {stack.front()};
			stack.pop_front();
			ret.push_front(current.path());
			if (current.is_directory()) {
				for (directory_iterator it {current.path()}; it != directory_iterator{}; ++it) {
					stack.push_back(*it);
				}
				// std::copy(directory_iterator{current.path()}, directory_iterator{}, stack.end() - 1);
			}
		}
		return ret;
	}

	static std::uintmax_t remove_all(const path& this_path) {
		assert(errno == 0);
		std::uintmax_t i = 0;
#ifdef FILESYSTEM_DEBUG
		std::cerr << "rm -rf " << this_path.string() << "\n";
#endif
		// I use a post-order traversal here, so that I am removing a dir AFTER removing its children.
		for (const directory_entry& descendent : post_order(this_path)) {
			++i;
			if (descendent.is_directory()) {
				assert(errno == 0);
#ifdef FILESYSTEM_DEBUG
				std::cerr << "rmdir " << descendent.path().string() << "\n";
#endif
				if (bool_unlikely(rmdir(descendent.path().c_str()))) {
					int errno_ = errno; errno = 0;
					throw filesystem_error(std::string{"rmdir"}, descendent.path(), errno_);
				}
			} else {
				assert(errno == 0);
#ifdef FILESYSTEM_DEBUG
				std::cerr << "unlink " << descendent.path().string() << "\n";
#endif
				if (bool_unlikely(unlink(descendent.path().c_str()))) {
					int errno_ = errno; errno = 0;
					throw filesystem_error(std::string{"unlink"}, descendent.path(), errno_);
				}
			}
		}
		return i;
	}

	static bool create_directory(const path& this_path) {
		std::error_code ec;
		directory_entry this_dir_ent{this_path, ec};
		if (!this_dir_ent.exists()) {
			mode_t umask_default = umask(0);
			umask(umask_default);
#ifdef FILESYSTEM_DEBUG
			std::cerr << "mkdir " << this_path.string() << "\n";
#endif
			int ret = mkdir(this_path.c_str(), 0777 & ~umask_default);
			if (ret == -1) {
				int errno_ = errno;
				errno = 0;
				throw filesystem_error(std::string{"mkdir"}, this_path, errno_);
			}
			return true;
		}
		return false;
	}

}
