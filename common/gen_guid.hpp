#pragma once
#include "phonebook.hpp"

#include <atomic>
#include <unordered_map>

namespace ILLIXR {
/**
 * @brief This class generates unique IDs
 *
 * If you need unique IDs (e.g. for each component), have each component call this class through
 * Phonebook. It returns unique IDs.
 *
 * You can use namespaces to express logical containment. The return value will be unique
 * _between other `get` calls to the same namespace_. This is useful for components and
 * sub-components. For example, If component with ID 0 has 3 subcomponents, one might call
 * get(0) to name each of them. Then, suppose component with ID 1 has 2 subcomponents, one might
 * call get(1) twice to name those. The subcomponent IDs could be reused (non-unique), but tuple
 * (component ID, subcomponent ID) will still be unique. You can also just use the global
 * namespace for everything, if you do not care about generating small integers for the IDs.
 *
 * This not-obsolete code was extracted from the obsolete record_logger.hpp.
 *
 */
class gen_guid : public phonebook::service {
public:
    /**
     * @brief Generate a number, unique from other calls to the same namespace/subnamespace/subsubnamepsace.
     */
    std::size_t get(std::size_t namespace_ = 0, std::size_t subnamespace = 0, std::size_t subsubnamespace = 0) {
        if (guid_starts[namespace_][subnamespace].count(subsubnamespace) == 0) {
            guid_starts[namespace_][subnamespace][subsubnamespace].store(1);
        }
        return guid_starts[namespace_][subnamespace][subsubnamespace]++;
    }

private:
    std::unordered_map<std::size_t, std::unordered_map<std::size_t, std::unordered_map<std::size_t, std::atomic<std::size_t>>>>
        guid_starts;
};
} // namespace ILLIXR
