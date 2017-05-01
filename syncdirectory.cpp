#include "syncdirectory.h"
#include <thread>
#include <iostream>
#include <sstream>
#include <list>
#pragma comment(lib, "mpr.lib")

const wchar_t spinner[4] = { L'|', L'/', L'-', L'\\' };
std::atomic<int> spinner_idx = 0;

bool write_file(const std::wstring &fname, const char *data, int len) {
	FILE *out = _wfopen(fname.c_str(), L"wb");
	if (!out)
		return false;
	bool rv = fwrite(data, len, 1, out) == 1;
	return fclose(out) == 0 && rv;
}

bool write_file(const std::wstring &fname, const std::string &data) {
	return write_file(fname, data.c_str(), data.length());
}

SyncDirectory::SyncDirectory(NETRESOURCE& resouce, const std::wstring& localpath) {
	connected_ = false;
	exit_ = false;
	resource_ = new NETRESOURCE;
	memcpy(resource_, &resouce, sizeof(NETRESOURCE));
  int rv = WNetCancelConnection2(resource_->lpLocalName, 0, true);
  std::wcerr << "(" << resource_->lpLocalName << ") Initial disconnection returned " << rv << std::endl;
	localpath_ = localpath;
  last_error_ = 0;
	std::wcerr << L"Creating SyncDirectory to " << localpath_ << std::endl;
	thread_ = new std::thread(&SyncDirectory::run, this);
}


SyncDirectory::~SyncDirectory() {
	exit_ = true;
	thread_->join();
	delete thread_;
	WNetCancelConnection2(resource_->lpLocalName, 0, true);
	delete[] resource_->lpLocalName;
	delete[] resource_->lpRemoteName;
	delete resource_;
}

void SyncDirectory::run() {
	int fail_count = 0;
	while (!exit_) {
		if (!connected_) {
			DWORD dwFlags = CONNECT_TEMPORARY;
			DWORD dwRetVal = 0;
			//std::wcerr << L"Trying to map " << resource_->lpRemoteName << " to " << resource_->lpLocalName << std::endl;
			dwRetVal = WNetAddConnection2(resource_, 0, 0, dwFlags);
			//std::wcerr << L"Got return status " << dwRetVal << L" from WNetAddConnection2 mapping " << resource_->lpRemoteName << " to " << resource_->lpLocalName << std::endl;
			if (dwRetVal == NO_ERROR || dwRetVal == ERROR_ALREADY_ASSIGNED) {
				DeleteFile((localpath_ + L"+OFFLINE.TXT").c_str());
        std::wcerr << localpath_ << L"(" << resource_->lpLocalName << L") : Connecting..." << std::endl;
				if (sync()) {
					std::wcerr << localpath_ << L"(" << resource_->lpLocalName << L") : Connected." << std::endl;
					connected_ = true;
				} else {
					std::wcerr << localpath_ << L": Sync failed (0)." << std::endl;
					fail_count++;
					if (fail_count > 10) {
						int rv = WNetCancelConnection2(resource_->lpLocalName, 0, true);
						std::wcerr << L"WNetCancelConnection2(" << resource_->lpLocalName << ") returns " << rv << std::endl;
						fail_count = 0;
						set_offline(1);
					}
				}
			}
			else {
				set_offline(dwRetVal);
			}
		} else {
			if (!sync()) {
				std::wcerr << localpath_ << L":B Sync failed. (1)" << std::endl;
				set_offline(0);
				int rv = WNetCancelConnection2(resource_->lpLocalName, 0, true);
				std::wcerr << L"WNetCancelConnection2(" << resource_->lpLocalName << ") returns " << rv << std::endl;
			}
			spinner_idx = (spinner_idx + 1) % 4;
			std::wcerr << '\b' << spinner[spinner_idx];
		}
		Sleep(1000);
	}
	std::cerr << "Thread Exited." << std::endl;
}

void delete_contents(const std::wstring& directory) {
	std::list<std::wstring> files;
	std::list<std::wstring> directories;
	WIN32_FIND_DATA ffd;
	HANDLE hfd = FindFirstFile((directory + L"*").c_str(), &ffd);
	if (hfd != INVALID_HANDLE_VALUE) {
		do {
			if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
				delete_contents(directory + ffd.cFileName + L"\\");
				directories.push_back(directory + ffd.cFileName);
			} else {
				files.push_back(directory + ffd.cFileName);
			}
		} while (FindNextFile(hfd, &ffd) != 0);
		FindClose(hfd);
	}
	for (auto it = files.begin(); it != files.end(); it++) {
		DeleteFile(it->c_str());
	}
	for (auto it = directories.begin(); it != directories.end(); it++) {
		RemoveDirectory(it->c_str());
	}
}

void SyncDirectory::clear_files() {
	delete_contents(localpath_);
	local_files_.clear();
}

bool get_files(const std::wstring& search_path, const std::wstring& dir_prefix, std::map<std::wstring, file_props_t>& files) {
	WIN32_FIND_DATA ffd;
	HANDLE hfd = FindFirstFile((search_path + L"*").c_str(), &ffd);
	if (hfd != INVALID_HANDLE_VALUE) {
		do {
			if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
			file_props_t prop;
			prop.attributes = ffd.dwFileAttributes;
			prop.creation_time.high = ffd.ftCreationTime.dwHighDateTime;
			prop.creation_time.low = ffd.ftCreationTime.dwLowDateTime;
			prop.write_time.high = ffd.ftLastWriteTime.dwHighDateTime;
			prop.write_time.low = ffd.ftLastWriteTime.dwLowDateTime;
			prop.size.high = ffd.nFileSizeHigh;
			prop.size.low = ffd.nFileSizeLow;
			std::wstring file = ffd.cFileName;
			for (size_t i = 0; i < file.size(); i++) {
				file[i] = ::towupper(file[i]);
			}
			files[search_path.substr(dir_prefix.length()) + file] = prop;
			if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				get_files(search_path + file + L"\\", dir_prefix, files);
			}
		} while (FindNextFile(hfd, &ffd) != 0);
		FindClose(hfd);
	} else {
		std::wcerr << "Error getting files from " << search_path << std::endl;
		return false;
	}
	return true;
}

std::wstring shortfile(const std::wstring& file, const std::wstring& search_path) {
	size_t extpos = file.rfind('.');
	std::wstring extpart;
	std::wstring filepart;
	if (extpos == std::string::npos) {
		extpos = file.length() + 1;
		filepart = file;
	} else {
		filepart = file.substr(0, extpos);
		extpart = file.substr(extpos + 1);
	}
	if (extpart.length() > 3) {
		extpart = extpart.substr(0, 3);
	}
	if (filepart.length() > 8) {
		filepart = filepart.substr(0, 6) + L"~1";
	}
	std::wstring newfile = filepart + (extpart.length() > 0 ? L"." : L"") + extpart;
	WIN32_FIND_DATA ffd;
	HANDLE hfd = FindFirstFile((search_path + newfile).c_str(), &ffd);
	while (hfd != INVALID_HANDLE_VALUE) {
		FindClose(hfd);
		filepart[7]++;
		if (filepart[7] == 0x3a) {
			filepart[7] = 0x41;
		}
		newfile = filepart + (extpart.length() > 0 ? L"." : L"") + extpart;
		hfd = FindFirstFile((search_path + newfile).c_str(), &ffd);
	}
	return newfile;
}

bool SyncDirectory::sync() {
	std::wstring src_path, dst_path;
	std::map<std::wstring, file_props_t> src_files, dst_files;
	std::map<std::wstring, file_props_t> *src_list = 0, *dst_list = 0;
	bool allow_delete = false;
	if (!connected_) {
		//std::wcerr << L"Doing initial population of " << localpath_;
		src_list = &remote_files_;
		dst_list = &local_files_;
		src_path = resource_->lpLocalName + std::wstring(L"\\");
		dst_path = localpath_;
		if (!get_files(src_path, resource_->lpLocalName + std::wstring(L"\\"), src_files)) {
			std::wcerr << "Get src files failed." << std::endl;
			return false;
		}
	} else {
		//std::wcerr << L"Sychronizing " << localpath_ << L" -> " << resource_->lpLocalName << std::endl;
		src_list = &local_files_;
		dst_list = &remote_files_;
		src_path = localpath_;
		dst_path = resource_->lpLocalName + std::wstring(L"\\");
		if (!get_files(localpath_, localpath_, src_files)) {
			std::wcerr << L"Local path has gone missing. Not doing anything." << std::endl;
			return true;
		}
		if (!get_files(dst_path, resource_->lpLocalName + std::wstring(L"\\"), dst_files)) {
			std::wcerr << L"Get dst files failed." << std::endl;
			return false;
		}
		allow_delete = true;
	}


	for (auto it = src_files.begin(); it != src_files.end(); it++) {
		auto jt = dst_files.find(it->first);
		auto kt = src_list->find(it->first);
		//std::wcerr << L"Checking file " << src_path << it->first << std::endl;
		if ((jt == dst_files.end() && (kt == src_list->end() || kt == dst_list->end())) || (kt != src_list->end() && !(it->second == kt->second))) {
			if (connected_) std::wcerr << L"Creating file " << dst_path << it->first << L" (connected = " << connected_ << L")" << std::endl;
			if (it->second.attributes & FILE_ATTRIBUTE_DIRECTORY) {
				if (CreateDirectory((dst_path + it->first).c_str(), 0) == 0) {
					unsigned long error = GetLastError();
					if (error != ERROR_ALREADY_EXISTS) {
						std::wcerr << L"(src->dst)CreateDirectory failed: " << error << std::endl;
						return false;
					}
				}
			} else {
				if (CopyFile((src_path + it->first).c_str(), (dst_path + it->first).c_str(), false) == 0) {
					unsigned long error = GetLastError();
					if (error == ERROR_SHARING_VIOLATION) {
						std::wcerr << L"(src->dst)File still open, postponing copy" << std::endl;
						continue;
					} else if (error == ERROR_INVALID_NAME) {
						std::wstring newfile = shortfile(it->first, src_path);
						std::wcerr << L"(src->dst)Filename to long. Renaming to " << newfile << std::endl;
						MoveFile((src_path + it->first).c_str(), (src_path + newfile).c_str());
						continue;
          } else if (error == ERROR_ACCESS_DENIED) {
            continue;
          } else if (error == ERROR_FILE_NOT_FOUND) {
            continue;
          }
					std::wcerr << L"(src->dst)CopyFile failed: " << error << std::endl;
					return false;
				}
			}
			WIN32_FIND_DATA ffd;
			HANDLE hfd = FindFirstFile((dst_path + it->first).c_str(), &ffd);
			if (hfd != INVALID_HANDLE_VALUE) {
				file_props_t prop;
				prop.attributes = ffd.dwFileAttributes;
				prop.creation_time.high = ffd.ftCreationTime.dwHighDateTime;
				prop.creation_time.low = ffd.ftCreationTime.dwLowDateTime;
				prop.write_time.high = ffd.ftLastWriteTime.dwHighDateTime;
				prop.write_time.low = ffd.ftLastWriteTime.dwLowDateTime;
				prop.size.high = ffd.nFileSizeHigh;
				prop.size.low = ffd.nFileSizeLow;
				(*dst_list)[it->first] = prop;
				(*src_list)[it->first] = it->second;
				dst_files[it->first] = prop;
				if (!(prop.size == it->second.size)) {
					std::wcerr << L"(src->dst)File Size Mismatch after Copy" << std::endl;
					(*src_list)[it->first].size.high = 0;
					(*src_list)[it->first].size.low = 0;
				}
				FindClose(hfd);
			} else {
				std::wcerr << L"(src->dst)Unable to obtain handle on copied file." << std::endl;
				return false;
			}
		}
	}

	for (auto it = dst_files.begin(); it != dst_files.end(); it++) {
		auto jt = src_files.find(it->first);
		auto kt = dst_list->find(it->first);
		//std::wcerr << L"Checking file " << dst_path << it->first << std::endl;
		if ((jt == src_files.end() && (kt == src_list->end() || kt == dst_list->end())) || (kt != dst_list->end() && !(it->second == kt->second))) {
			if (connected_) std::wcerr << L"(dst->src)Creating file " << src_path << it->first << L" (connected = " << connected_ << L")" << std::endl;
			if (it->second.attributes & FILE_ATTRIBUTE_DIRECTORY) {
				if (CreateDirectory((src_path + it->first).c_str(), 0) == 0) {
					unsigned long error = GetLastError();
					if (error != ERROR_ALREADY_EXISTS) {
						std::wcerr << L"(dst->src)CreateDirectory failed: " << error << std::endl;
						return false;
					}
				}
			} else {
				if (CopyFile((dst_path + it->first).c_str(), (src_path + it->first).c_str(), false) == 0) {
					unsigned long error = GetLastError();
					if (error == ERROR_SHARING_VIOLATION) {
						std::wcerr << L"(dst->src)File still open, postponing copy" << std::endl;
						continue;
					} else if (error == ERROR_INVALID_NAME) {
						std::wstring newfile = shortfile(it->first, dst_path);
						std::wcerr << L"(dst->src)Filename to long. Renaming to " << newfile << std::endl;
						MoveFile((dst_path + it->first).c_str(), (dst_path + newfile).c_str());
						continue;
          } else if (error == ERROR_ACCESS_DENIED) {
            continue;
          } else if (error == ERROR_FILE_NOT_FOUND) {
            continue;
          }
					std::wcerr << L"(dst->src)CopyFile failed: " << error << std::endl;
				}
			}
			WIN32_FIND_DATA ffd;
			HANDLE hfd = FindFirstFile((src_path + it->first).c_str(), &ffd);
			if (hfd != INVALID_HANDLE_VALUE) {
				file_props_t prop;
				prop.attributes = ffd.dwFileAttributes;
				prop.creation_time.high = ffd.ftCreationTime.dwHighDateTime;
				prop.creation_time.low = ffd.ftCreationTime.dwLowDateTime;
				prop.write_time.high = ffd.ftLastWriteTime.dwHighDateTime;
				prop.write_time.low = ffd.ftLastWriteTime.dwLowDateTime;
				prop.size.high = ffd.nFileSizeHigh;
				prop.size.low = ffd.nFileSizeLow;
				(*src_list)[it->first] = prop;
				(*dst_list)[it->first] = it->second;
				src_files[it->first] = prop;
				if (!(prop.size == it->second.size)) {
					std::wcerr << L"(dst->src)File Size Mismatch after Copy" << std::endl;
					(*dst_list)[it->first].size.high = 0;
					(*dst_list)[it->first].size.low = 0;
				}
				FindClose(hfd);
			}
			else {
				std::wcerr << L"(dst->src)Unable to obtain handle on copied file." << std::endl;
				return false;
			}
		}
	}

	if (allow_delete) {
		for (auto it = src_list->begin(); it != src_list->end();) {
			auto jt = src_files.find(it->first);
			if (jt == src_files.end()) {
				int rv = 0;
				auto lt = dst_list->find(it->first);
				if (lt != dst_list->end()) {
					std::wcerr << L"Deleting file from dst " << dst_path + it->first;
					rv = DeleteFile((dst_path + it->first).c_str());
					int rerr = GetLastError();
					std::wcerr << L"\tReturned (" << rv << L"," << rerr << L")" << std::endl;
					if (rv) {
						dst_list->erase(lt);
					}
				}
				if (rv) {
					auto kt = it;
					it++;
					src_list->erase(kt);
				} else {
					it++;
				}
			} else {
				it++;
			}
		}
		for (auto it = dst_list->begin(); it != dst_list->end();) {
			auto jt = dst_files.find(it->first);
			if (jt == dst_files.end()) {
				int rv = 0;
				auto lt = src_list->find(it->first);
				if (lt != src_list->end()) {
					std::wcerr << L"Deleting file from src " << src_path + it->first;
					rv = DeleteFile((src_path + it->first).c_str());
					int rerr = GetLastError();
					std::wcerr << L"\tReturned (" << rv << L"," << rerr << L")" << std::endl;
					if (rv) {
						src_list->erase(lt);
					}
				}
				if (rv) {
					auto kt = it;
					it++;
					dst_list->erase(kt);

				} else {
					it++;
				}
			} else {
				it++;
			}
		}
	}
	return true;
}

void SyncDirectory::set_offline(unsigned long error) {
	/*if (connected_) {
		clear_files();
	}*/
	std::stringstream ss;
	ss << "OFFLINE: Connection returned error " << error;
	write_file(localpath_ + L"+OFFLINE.TXT", ss.str());
	//std::wcerr << "Wrote " << localpath_ << L"OFFLINE.TXT" << std::endl;
	connected_ = false;
}
