#include "cereal/archives/binary.hpp"
#include "cereal/cereal.hpp"
#include "cereal/types/string.hpp"
#include "cereal/types/vector.hpp"
#include "picosha2.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <omp.h>
#include <span>
#include <stack>
#include <stdexcept>
#include <string>
#include <vector>
struct Record
{
  Record(const std::filesystem::directory_entry &file)
  {
    this->filePath = file.path().string();
    if (std::filesystem::is_directory(file))
    {
      return;
    }
    std::ifstream fileStream(file.path(), std::ios::binary);
    std::vector<unsigned char> hash(picosha2::k_digest_size);
    picosha2::hash256(fileStream, hash.begin(), hash.end());
    std::ostringstream oss;
    for (unsigned char byte : hash)
    {
      oss << std::hex << std::setw(2) << std::setfill('0')
          << static_cast<int>(byte);
    }
    this->hashFile = oss.str();
  }
  template<class Archive>
  void
  serialize(Archive &archive)
  {
    archive(this->filePath, this->hashFile);
  }
  [[nodiscard]]
  auto
  getFilePath() const
  {
    return this->filePath;
  }
  [[nodiscard]]
  auto
  getHash() const
  {
    return this->hashFile;
  }

private:
  std::string filePath;
  std::string hashFile;
};
namespace
{
auto
listAllDir(const std::filesystem::directory_entry &dir)
  -> std::vector<std::filesystem::directory_entry>
{
  std::vector<std::filesystem::directory_entry> listFile;
  std::stack<std::filesystem::directory_entry> dirsToProcess;
  dirsToProcess.push(dir);
  while (!dirsToProcess.empty())
  {
    auto currentDir = dirsToProcess.top();
    dirsToProcess.pop();
    for (const auto &dirEntry :
         std::filesystem::directory_iterator{ currentDir })
    {
      if (std::filesystem::is_directory(dirEntry))
      {
        dirsToProcess.push(dirEntry);
      }
      listFile.push_back(dirEntry);
    }
  }
  return listFile;
}

template<class T>
void
snapshot(std::span<T> args)
{
  auto rootDir = static_cast<std::filesystem::directory_entry>(args[1]);
  if (!std::filesystem::is_directory(rootDir))
  {
    throw std::invalid_argument("not a directory");
  }
  if (!std::filesystem::exists(rootDir))
  {
    throw std::invalid_argument("dir not exists");
  }
  std::vector<std::filesystem::directory_entry> listFiles =
    listAllDir(rootDir);
  std::vector<Record> listRecord;
#pragma omp parallel
  for (auto const &file : listFiles)
  {
    Record record(file);
#pragma omp critical
    {
      listRecord.push_back(record);
    }
  }
  auto absolutePath = std::filesystem::absolute(rootDir).string();
  absolutePath.resize(absolutePath.size() - 2);
  auto archiveName = absolutePath + ".bin";
  std::ofstream archiveStream(archiveName, std::ios::binary);
  cereal::BinaryOutputArchive archive(archiveStream);
  archive(listRecord);
}
}

auto
main(int argc, char *argv[]) -> int
{
  auto args = std::span(argv, size_t(argc));
  if (argc != 2)
  {
    throw std::invalid_argument("pass in argument path of dir to snapshot");
  }
  snapshot(args);
  return 0;
}
