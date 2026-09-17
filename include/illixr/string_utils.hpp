/*!
 * @file
 * @brief  String utility functions
 * @author RSIM Group <illixr@cs.illinois.edu>
 */

#pragma once

#include <sstream>
#include <string>
#include <vector>

namespace ILLIXR {

/*!
 * @brief Splits a string into tokens by a delimiter character.
 * @param s The string to split.
 * @param delimiter The character to split on.
 * @return A vector of token strings.
 */
inline std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string              token;
    std::istringstream       token_stream{s};
    while (std::getline(token_stream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

} // namespace ILLIXR
