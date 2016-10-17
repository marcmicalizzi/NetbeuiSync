#pragma once

#include <string>
#include <map>
#include <set>
#include <atomic>
#include <Windows.h>
#include <Winnetwk.h>
#include <iostream>

struct long_hl {
	unsigned long high;
	unsigned long low;
	bool operator==(const long_hl& rhs) {
		return high == rhs.high && low == rhs.low;
	}
};

struct file_props_t {
	unsigned long attributes;
	long_hl creation_time;
	long_hl write_time;
	long_hl size;
	bool operator==(const file_props_t& rhs) {
		if (attributes != rhs.attributes) std::wcerr << "dwAttr doesn't match" << std::endl;
		if (!(creation_time == rhs.creation_time)) std::wcerr << "Creation time doesn't match" << std::endl;
		if (!(write_time == rhs.write_time)) std::wcerr << "Write time doesn't match" << std::endl;
		if (!(size == rhs.size)) std::wcerr << "Size doesn't match" << std::endl;
		return attributes == rhs.attributes &&
			   creation_time == rhs.creation_time &&
			   write_time == rhs.write_time &&
			   size == rhs.size;
	}
};
namespace std {
	class thread;
};
class SyncDirectory {
	public:
		SyncDirectory(NETRESOURCE& resouce, const std::wstring& localpath);//takes ownership of resource strings
		~SyncDirectory();
	private:
		std::map<std::wstring, file_props_t> local_files_;
		std::map<std::wstring, file_props_t> remote_files_;
		std::wstring localpath_;
		std::thread* thread_;
		std::atomic<bool> exit_;
		NETRESOURCE* resource_;
		bool connected_;

		void run();
		void clear_files();
		bool sync();
		void set_offline(unsigned long error);
};

