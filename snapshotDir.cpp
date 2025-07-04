#include "argparse.hpp"
#include "cereal/archives/binary.hpp"
#include "cereal/cereal.hpp"
#include "cereal/types/string.hpp"
#include "cereal/types/vector.hpp"
#include "picosha2.h"
#ifndef _WIN32
#include <omp.h>
#endif
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <stack>
#include <stdexcept>
#include <string>
#include <vector>
#define VERSION "0.0.1"
#define PACKAGE "snapshotDir"
#define PACKAGE_STRING PACKAGE VERSION
struct Record
{
  explicit Record() = default;
  explicit Record(const std::filesystem::path &file)
  {
    this->filePath = file.string();
    if (std::filesystem::is_directory(file))
    {
      return;
    }
    try
    {
      std::ifstream fileStream(file, std::ios::binary);
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
  auto
  operator==(const Record &other) const
  {
    auto testFilePath = this->filePath == other.getFilePath();
    auto testHashFile = this->hashFile == other.getHash();
    return testFilePath && testHashFile;
  }

private:
  std::string filePath;
  std::string hashFile;
};

struct Snapshot
{
  std::string pathOfSnapshot;
  std::vector<Record> listRecord;
  template<class Archive>
  auto
  serialize(Archive &archive)
  {
    archive(this->pathOfSnapshot, this->listRecord);
  }
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
findElementsNotInBoth(const std::vector<Record> &vec1,
                      const std::vector<Record> &vec2)
{
  std::vector<Record> result;
  auto notInVec2 =
    vec1 | std::views::filter(
             [&vec2](const Record &elem)
             { return std::ranges::find(vec2, elem) == vec2.end(); });
  auto notInVec1 =
    vec2 | std::views::filter(
             [&vec1](const Record &elem)
             { return std::ranges::find(vec1, elem) == vec1.end(); });
  result.insert(result.end(), notInVec2.begin(), notInVec2.end());
  result.insert(result.end(), notInVec1.begin(), notInVec1.end());
  return result;
}

auto
snapshot(const std::string &path)
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
    Record record(std::filesystem::absolute(file));
#pragma omp critical
    {
      listRecord.push_back(record);
    }
  }
  auto absolutePath = std::filesystem::absolute(rootDir);
  auto archiveName = absolutePath.string() + "snapshot.bin";
  std::ofstream archiveStream(archiveName, std::ios::binary);
  cereal::BinaryOutputArchive archive(archiveStream);
  Snapshot snapshotData;
  snapshotData.listRecord = std::move(listRecord);
  snapshotData.pathOfSnapshot = std::move(absolutePath);
  archive(snapshotData);
}

auto
check(const std::string &snapshotFile)
{
  auto archivePath =
    static_cast<std::filesystem::directory_entry>(snapshotFile);
  if (!std::filesystem::exists(archivePath))
  {
    throw std::invalid_argument("snapshot file not exists");
  }
  std::ifstream archiveStream(snapshotFile, std::ios::binary);
  cereal::BinaryInputArchive archive(archiveStream);
  Snapshot snapshot;
  archive(snapshot);
  std::vector<Record> listRecordOld = snapshot.listRecord;
  const auto rootDir =
    static_cast<std::filesystem::directory_entry>(snapshot.pathOfSnapshot);
  const std::vector<std::filesystem::directory_entry> listFiles =
    listAllDir(rootDir);
  std::vector<Record> listRecordNew;
#pragma omp parallel
  for (auto const &file : listFiles)
  {
    Record record(file);
#pragma omp critical
    {
      listRecordNew.push_back(record);
    }
  }
  auto printStatus = [=](const auto &status, const auto &record)
  {
    std::printf("[%s] : %s\n",
                static_cast<const char *>(status),
                record.getFilePath().c_str());
  };
  auto notBoth = findElementsNotInBoth(listRecordOld, listRecordNew);
  std::vector<std::string> listFilesChange;
  for (auto const &record : notBoth)
  {
    auto findOld = std::ranges::find_if(
      listRecordOld,
      [=](const auto &recordTest)
      { return recordTest.getFilePath() == record.getFilePath(); });
    auto findNew = std::ranges::find_if(
      listRecordNew,
      [=](const auto &recordTest)
      { return recordTest.getFilePath() == record.getFilePath(); });
    auto isInOld = findOld != listRecordOld.end();
    auto isInNew = findNew != listRecordNew.end();
    if (!isInOld && isInNew)
    {
      printStatus("CREETE", record);
      continue;
    }
    if (isInOld && !isInNew)
    {
      printStatus("DELETE", record);
      continue;
    }
    const auto testIfInListFilesChange =
      std::ranges::find(listFilesChange, findNew->getFilePath()) !=
      listFilesChange.end();
    if (testIfInListFilesChange)
    {
      continue;
    }
    auto notSameHash = findNew->getHash() != findOld->getHash();
    if (notSameHash)
    {
      printStatus("CHANGE", record);
      listFilesChange.push_back(findNew->getFilePath());
      continue;
    }
    throw std::runtime_error("file are identical");
  }
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
    .required()
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
    auto snapshotFile = checkPaser.get<std::string>("snapshotFile");
    check(snapshotFile);
    return 0;
  }
  std::cerr << program;
  return 1;
}
