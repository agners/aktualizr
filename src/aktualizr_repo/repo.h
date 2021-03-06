#ifndef REPO_H_
#define REPO_H_

#include <fnmatch.h>

#include <crypto/crypto.h>
#include <boost/filesystem.hpp>
#include "json/json.h"
#include "uptane/tuf.h"

struct KeyPair {
  KeyPair() = default;
  KeyPair(PublicKey public_key_in, std::string private_key_in)
      : public_key(std::move(public_key_in)), private_key(std::move(private_key_in)) {}
  PublicKey public_key;
  std::string private_key;
};

struct Delegation {
  Delegation() = default;
  Delegation(const boost::filesystem::path &repo_path, std::string delegation_name) : name(std::move(delegation_name)) {
    if (Uptane::Role::IsReserved(name)) {
      throw std::runtime_error("Delegation name " + name + " is reserved.");
    }
    boost::filesystem::path delegation_path(((repo_path / "repo/image/delegations") / name).string() + ".json");
    boost::filesystem::path targets_path(repo_path / "repo/image/targets.json");
    if (!boost::filesystem::exists(delegation_path) || !boost::filesystem::exists(targets_path)) {
      throw std::runtime_error(std::string("delegation ") + delegation_path.string() + " does not exist");
    }

    pattern = findPatternInTree(repo_path, name, Utils::parseJSONFile(targets_path)["signed"]);

    if (pattern.empty()) {
      throw std::runtime_error("Could not find delegation role in the delegation tree");
    }
  }
  bool isMatched(const boost::filesystem::path &image_path) {
    return (fnmatch(pattern.c_str(), image_path.c_str(), 0) == 0);
  }
  operator bool() const { return (!name.empty() && !pattern.empty()); }
  std::string name;
  std::string pattern;

 private:
  static std::string findPatternInTree(const boost::filesystem::path &repo_path, const std::string &name,
                                       const Json::Value &targets_json);
};

class Repo {
 public:
  Repo(Uptane::RepositoryType repo_type, boost::filesystem::path path, const std::string &expires,
       std::string correlation_id);
  void generateRepo(KeyType key_type = KeyType::kRSA2048);
  Json::Value getTarget(const std::string &target_name);
  Json::Value signTuf(const Uptane::Role &role, const Json::Value &json);

 protected:
  void generateRepoKeys(KeyType key_type);
  void generateKeyPair(KeyType key_type, const Uptane::Role &key_name);
  std::string getExpirationTime(const std::string &expires);
  void readKeys();
  void updateRepo();
  Uptane::RepositoryType repo_type_;
  boost::filesystem::path path_;
  std::string correlation_id_;
  std::string expiration_time_;
  std::map<Uptane::Role, KeyPair> keys_;

 private:
  void addDelegationToSnapshot(Json::Value *snapshot, const Uptane::Role &role);
};

#endif  // REPO_H_
