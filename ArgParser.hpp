#ifndef ArgParser_hpp
#define ArgParser_hpp

#include <functional>
#include <vector>
#include <utility>
#include <string>
#include <set>
#include <unordered_map>
#include <optional>

struct ArgParser {
private:
    std::set<std::string> options{};
    std::set<std::string> flags{};
    std::unordered_map<std::string, std::string> shortToLongMap{};
public:
    ArgParser(std::vector<std::pair<std::string, std::string>> options, std::vector<std::pair<std::string, std::string>> flags);
    std::optional<std::string> toKey(std::string prefixedKey);
    bool parse(int argc, char **argv, std::function<bool (std::string, std::string)> onOption, std::function<bool (std::string)> onFlag);
};

#endif /* ArgParser_hpp */
