#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <regex>
#include <thread>
#include <vector>

#include "seed.hpp"

using std::cout;
using std::endl;
using std::fstream;
using std::ios;
using std::lock_guard;
using std::move;
using std::mutex;
using std::regex;
using std::regex_replace;
using std::string;
using std::thread;
using std::unique_ptr;
using std::vector;

namespace fs = std::filesystem;

mutex mtx;

template <typename T>
void safe_out(const T &data) {
  lock_guard<mutex> lock(mtx);
  cout << data << endl;
}

void sub_process(const string &dir) {
  safe_out("decode: " + dir);
  fstream infile(dir, ios::in | ios::binary);
  if (!infile.is_open()) {
    safe_out("qmc file read error");
    return;
  }

  string outloc(move(dir));
  const regex mp3_regex{"\\.(qmc3|qmc0|qmcogg)$"}, flac_regex{"\\.qmcflac$"};
  auto mp3_outloc = regex_replace(outloc, mp3_regex, ".mp3");
  auto flac_outloc = regex_replace(outloc, flac_regex, ".flac");
  outloc = (outloc != mp3_outloc ? mp3_outloc : flac_outloc);
  auto len = infile.seekg(0, ios::end).tellg();
  infile.seekg(0, ios::beg);
  char *buffer = new (std::nothrow) char[len];
  if (buffer == nullptr) {
    safe_out("create buffer error");
    return;
  }
  unique_ptr<char[]> auto_delete(buffer);

  infile.read(buffer, len);
  infile.close();

  qmc_decoder::seed seed_;
  for (int i = 0; i < len; ++i) {
    buffer[i] = seed_.next_mask() ^ buffer[i];
  }

  fstream outfile(outloc.c_str(), ios::out | ios::binary);

  if (outfile.is_open()) {
    outfile.write(buffer, len);
    outfile.close();
  } else {
    safe_out("open dump file error");
  }
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr
        << "filenames shall be passed via args..."
        << std::endl;
    return -1;
  }

  if ((fs::status(fs::path(".")).permissions() & fs::perms::owner_write) ==
      fs::perms::none) {
    std::cerr << "please check if you have the write permissions on current dir."
              << std::endl;
    return -1;
  }
  vector<string> qmc_paths;
  const regex qmc_regex{"^.+\\.(qmc3|qmc0|qmcflac|qmcogg)$"};

  for (int i = 1; i < argc; ++i) {
      auto absp = fs::absolute(fs::path{argv[i]});
      if (fs::exists(absp) &&
          (fs::status(absp).permissions() & fs::perms::owner_read) != fs::perms::none &&
          fs::is_regular_file(absp) && regex_match(absp.string(), qmc_regex)) {
          qmc_paths.emplace_back(std::move(absp.string()));
      }
      else {
          std::cout << "check argument: " << argv[i] << " you give to this program..." << std::endl;
          if (!fs::exists(absp)) std::cout << "Nonexistance!" << std::endl;
          if ((fs::status(absp).permissions() & fs::perms::owner_read) == fs::perms::none) std::cout << "Non readable permission!" << std::endl;
          if (fs::is_regular_file(absp) && regex_match(absp.string(), qmc_regex)) std::cout << "Not a proper target for conversion!" << std::endl;
      }
  }

  const auto n_thread = thread::hardware_concurrency();
  vector<thread> td_group;

  for (size_t i = 0; i < n_thread - 1; ++i) {
    td_group.emplace_back(
        [&qmc_paths, &n_thread](int index) {
          for (size_t j = index; j < qmc_paths.size(); j += n_thread) {
            sub_process(qmc_paths[j]);
          }
        },
        i);
  }

  for (auto &&td : td_group) {
    td.join();
  }

  return 0;
}
