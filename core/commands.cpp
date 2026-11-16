

#include "commands.hpp"

#include <map>

namespace {

/** @brief Return the process-wide registry, initialized on first use. */
std::map<std::string, CommandFunc>& commandRegistry()
{
    static std::map<std::string, CommandFunc> registry;
    return registry;
}

} // namespace

prvcmd::Register::Register(const char* name, CommandFunc func)
{
    if (name == nullptr || func == nullptr) {
        return;
    }

    commandRegistry()[name] = func;
}

BinJSON::Any executeCommand(BinJSON::Any command)
{
    if (!command) {
        return BinJSON::makeError("Missing command request");
    }

    const BinJSON::Any& commandName = command->get("command");
    if (!commandName || commandName->getType() != BinJSON::BinJSONType::STRING) {
        return BinJSON::makeError("Command request must contain a string command name");
    }

    const std::string& name = commandName->string();
    const auto& registry = commandRegistry();
    const auto registeredCommand = registry.find(name);
    if (registeredCommand == registry.end()) {
        return BinJSON::makeError("Unknown command: " + name);
    }

    return registeredCommand->second(command);
}
