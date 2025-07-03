#include "argparse.hpp"
#include "cereal/archives/binary.hpp"
#include "cereal/cereal.hpp"
#include "cereal/types/string.hpp"
#include "cereal/types/vector.hpp"
#include "picosha2.h"
#include <filesystem>
#include <fstream>
#ifndef _WIN32
#include <omp.h>
#endif
#include <stack>
#include <stdexcept>
#include <string>
#include <vector>
#define VERSION "0.0.1"
#define PACKAGE "snapshotDir"
#define PACKAGE_STRING PACKAGE VERSION
struct Record
{
  Record(const std::filesystem::directory_entry &file)
  {
    this->filePath = file.path().string();
    if (std::filesystem::is_directory(file))
    {
      return;
    }
    try
    {
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
    catch (const std::exception &err)
    {
      std::cerr << err.what() << "\n";
      this->hashFile = "notHash";
    }
  }
  template<class Archive>
  auto
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
    try
    {
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
    catch (const std::exception &err)
    {
      std::cerr << err.what() << "\n";
    }
  }
  return listFile;
}

auto
snapshot(std::string &path)
{
  auto rootDir = static_cast<std::filesystem::directory_entry>(path);
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
  auto archiveName =  absolutePath + "snapshot.bin";
  std::ofstream archiveStream(archiveName, std::ios::binary);
  cereal::BinaryOutputArchive archive(archiveStream);
  archive(listRecord);
}
}

auto
main(int argc, char *argv[]) -> int
{
  argparse::ArgumentParser program("snapshotDir", PACKAGE_STRING);
  argparse::ArgumentParser snapshotParser("snapshot");
  snapshotParser.add_description("take a snapshot of dir");
  snapshotParser.add_argument("dir")
    .help("dirs to take a snapshot")
    .default_value(".")
    .metavar("PATH_OF_DIR");
  program.add_subparser(snapshotParser);
  argparse::ArgumentParser checkPaser("check");
  checkPaser.add_description("check if file has changed");
  checkPaser.add_argument("snapshotFile")
    .help("file of snapshot to check")
    .metavar("FILE");
  program.add_subparser(checkPaser);
  try
  {
    program.parse_args(argc, argv);
  }
  catch (const std::exception &err)
  {
    std::cerr << err.what() << "\n";
    std::cerr << program;
    return 1;
  }
  if (program.is_subcommand_used(snapshotParser))
  {
    auto dir = snapshotParser.get<std::string>("dir");
    snapshot(dir);
    return 0;
  }
  if (program.is_subcommand_used(checkPaser))
  {
    throw std::runtime_error("notImplemented");
    return 0;
  }
  std::cerr << program;
  return 1;
}
