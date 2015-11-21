#pragma once

#include <ostream>
#include <stdint.h>
#include "utils/total_ordering.hpp"

class Id : public TotalOrdering<Id>
{
public:
    Id(uint64_t id) : id(id) {}

    friend bool operator<(const Id& a, const Id& b)
    {
        return a.id < b.id;
    }

    friend bool operator==(const Id& a, const Id& b)
    {
        return a.id == b.id;
    }

    friend std::ostream& operator<<(std::ostream& stream, const Id& id)
    {
        return stream << id.id;
    }
    
private:
    uint64_t id;
};
