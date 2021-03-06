#include <boost/program_options.hpp>
#include <iostream>
#include <istream>
#include <iterator>
#include <ostream>
#include <string>

#include "logging/logging.h"
#include "uptane_repo.h"

namespace po = boost::program_options;

int main(int argc, char **argv) {
  po::options_description desc("aktualizr-repo command line options");
  // clang-format off
  desc.add_options()
    ("help,h", "print usage")
    ("command", po::value<std::string>(), "generate: \tgenerate a new repository\n"
                                          "adddelegation: \tadd a delegated role to the images metadata\n"
                                          "revokedelegation: \tremove delegated role from the images metadata and all signed targets of this role\n"
                                          "image: \tadd a target to the images metadata\n"
                                          "addtarget: \tprepare director targets metadata for a given device\n"
                                          "signtargets: \tsign the staged director targets metadata\n"
                                          "emptytargets: \tclear the staged director targets metadata\n"
                                          "oldtargets: \tfill the staged director targets metadata with what is currently signed\n"
                                          "sign: \tsign arbitrary metadata with repo keys")
    ("path", po::value<boost::filesystem::path>(), "path to the repository")
    ("filename", po::value<boost::filesystem::path>(), "path to the image")
    ("hwid", po::value<std::string>(), "target hardware identifier")
    ("serial", po::value<std::string>(), "target ECU serial")
    ("expires", po::value<std::string>(), "expiration time")
    ("keyname", po::value<std::string>(), "name of key's role")
    ("repotype", po::value<std::string>(), "director|image")
    ("correlationid", po::value<std::string>()->default_value(""), "correlation id")
    ("keytype", po::value<std::string>()->default_value("RSA2048"), "UPTANE key type")
    ("targetname", po::value<std::string>(), "target's name (if different than filename)")
    ("targetsha256", po::value<std::string>(), "target's SHA256 hash (for adding metadata without an actual file)")
    ("targetsha512", po::value<std::string>(), "target's SHA512 hash (for adding metadata without an actual file)")
    ("targetlength", po::value<uint64_t>(), "target's length (for adding metadata without an actual file)")
    ("dname", po::value<std::string>(), "delegated role name")
    ("dterm", po::bool_switch(), "if the created delegated role is terminating")
    ("dparent", po::value<std::string>()->default_value("targets"), "delegated role parent name")
    ("dpattern", po::value<std::string>(), "delegated file path pattern");

  // clang-format on

  po::positional_options_description positionalOptions;
  positionalOptions.add("command", 1);
  positionalOptions.add("path", 1);
  positionalOptions.add("filename", 1);

  try {
    logger_init();
    logger_set_threshold(boost::log::trivial::info);
    po::variables_map vm;
    po::basic_parsed_options<char> parsed_options =
        po::command_line_parser(argc, argv).options(desc).positional(positionalOptions).run();
    po::store(parsed_options, vm);
    po::notify(vm);
    if (vm.count("help") != 0) {
      std::cout << desc << std::endl;
      exit(EXIT_SUCCESS);
    }

    if (vm.count("command") != 0 && vm.count("path") != 0) {
      std::string expiration_time;
      if (vm.count("expires") != 0) {
        expiration_time = vm["expires"].as<std::string>();
      }
      std::string correlation_id;
      if (vm.count("correlationid") != 0) {
        correlation_id = vm["correlationid"].as<std::string>();
      }
      UptaneRepo repo(vm["path"].as<boost::filesystem::path>(), expiration_time, correlation_id);
      std::string command = vm["command"].as<std::string>();
      if (command == "generate") {
        std::string key_type_arg = vm["keytype"].as<std::string>();
        std::istringstream key_type_str{std::string("\"") + key_type_arg + "\""};
        KeyType key_type;
        key_type_str >> key_type;
        repo.generateRepo(key_type);
      } else if (command == "image") {
        if (vm.count("targetname") == 0 && vm.count("filename") == 0) {
          std::cerr << "image command requires --targetname or --filename\n";
          exit(EXIT_FAILURE);
        }
        auto targetname = (vm.count("targetname") > 0) ? vm["targetname"].as<std::string>()
                                                       : vm["filename"].as<boost::filesystem::path>();

        Delegation delegation;
        if (vm.count("dname") != 0) {
          delegation = Delegation(vm["path"].as<boost::filesystem::path>(), vm["dname"].as<std::string>());
          if (!delegation.isMatched(targetname)) {
            std::cerr << "Image path doesn't match delegation!\n";
            exit(EXIT_FAILURE);
          }
        }
        if (vm.count("filename") > 0) {
          repo.addImage(vm["filename"].as<boost::filesystem::path>(), targetname, delegation);
        } else {
          if ((vm.count("targetsha256") == 0 && vm.count("targetsha512") == 0) || vm.count("targetlength") == 0) {
            std::cerr << "image command requires --targetsha256 or --targetsha512, and --targetlength when --filename "
                         "is not supplied.\n";
            exit(EXIT_FAILURE);
          }
          std::unique_ptr<Uptane::Hash> hash;
          if (vm.count("targetsha256") > 0) {
            hash = std_::make_unique<Uptane::Hash>(Uptane::Hash::Type::kSha256, vm["targetsha256"].as<std::string>());
          } else {
            hash = std_::make_unique<Uptane::Hash>(Uptane::Hash::Type::kSha512, vm["targetsha512"].as<std::string>());
          }
          repo.addCustomImage(targetname.string(), *hash, vm["targetlength"].as<uint64_t>(), delegation);
        }
      } else if (command == "addtarget") {
        if (vm.count("targetname") == 0 || vm.count("hwid") == 0 || vm.count("serial") == 0) {
          std::cerr << "addtarget command requires --targetname, --hwid, and --serial\n";
          exit(EXIT_FAILURE);
        }
        repo.addTarget(vm["targetname"].as<std::string>(), vm["hwid"].as<std::string>(),
                       vm["serial"].as<std::string>());
      } else if (command == "adddelegation") {
        if (vm.count("dname") == 0 || vm.count("dpattern") == 0) {
          std::cerr << "adddelegation command requires --dname and --dpattern\n";
          exit(EXIT_FAILURE);
        }
        std::string dparent = vm["dparent"].as<std::string>();
        repo.addDelegation(Uptane::Role(vm["dname"].as<std::string>(), true),
                           Uptane::Role(dparent, dparent != "targets"), vm["dpattern"].as<std::string>(),
                           vm["dterm"].as<bool>());
      } else if (command == "revokedelegation") {
        if (vm.count("dname") == 0) {
          std::cerr << "revokedelegation command requires --dname\n";
          exit(EXIT_FAILURE);
        }
        repo.revokeDelegation(Uptane::Role(vm["dname"].as<std::string>(), true));
      } else if (command == "signtargets") {
        repo.signTargets();
      } else if (command == "emptytargets") {
        repo.emptyTargets();
      } else if (command == "oldtargets") {
        repo.oldTargets();
      } else if (command == "sign") {
        if (vm.count("repotype") == 0 || vm.count("keyname") == 0) {
          std::cerr << "sign command requires --repotype and --keyname\n";
          exit(EXIT_FAILURE);
        }
        std::cin >> std::noskipws;
        std::istream_iterator<char> it(std::cin);
        std::istream_iterator<char> end;
        std::string text_to_sign(it, end);

        Repo base_repo(Uptane::RepositoryType(vm["repotype"].as<std::string>()),
                       vm["path"].as<boost::filesystem::path>(), expiration_time, correlation_id);

        auto json_to_sign = Utils::parseJSON(text_to_sign);
        if (json_to_sign == Json::nullValue) {
          std::cerr << "Text to sign must be valid json\n";
          exit(EXIT_FAILURE);
        }

        auto json_signed = base_repo.signTuf(Uptane::Role(vm["keyname"].as<std::string>()), json_to_sign);
        std::cout << Utils::jsonToCanonicalStr(json_signed);
      } else {
        std::cout << desc << std::endl;
        exit(EXIT_FAILURE);
      }
    } else {
      std::cout << desc << std::endl;
      exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);

  } catch (const po::error &o) {
    std::cout << o.what() << std::endl;
    std::cout << desc;
    return EXIT_FAILURE;
  }
}
// vim: set tabstop=2 shiftwidth=2 expandtab:
