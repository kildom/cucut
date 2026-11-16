
#ifndef CUCUT_COMMANDS_HPP
#define CUCUT_COMMANDS_HPP

#include <string>

#include "binjson.hpp"

/**
 * @brief Function type used to execute a binJSON command.
 * @param command The command request.
 * @return The command response.
 */
using CommandFunc = BinJSON::Any (*)(BinJSON::Any command);

/**
 * @brief Register a command function during static initialization.
 * @param name A command function whose identifier is also used as its command name.
 */
#define REGISTER_COMMAND(name) prvcmd::Register _##name##Registration(#name, name)

namespace prvcmd {

/** @brief Registers a command function under the provided name. */
struct Register
{
    /**
     * @brief Add or replace a command in the command registry.
     * @param name Command name; null names are ignored.
     * @param func Function invoked for the command; null functions are ignored.
     */
    Register(const char* name, CommandFunc func);
};

} // namespace prvcmd

/**
 * @brief Dispatch a command described by a binJSON object.
 * @param command Request object containing a string-valued @c command property.
 * @return The command result, or a binJSON error if the request has no valid
 *         command name or the named command is not registered.
 */
BinJSON::Any executeCommand(BinJSON::Any command);

#endif // CUCUT_COMMANDS_HPP

