#include <Storages/MergeTree/ActiveDataPartSet.h>


namespace DB
{

ActiveDataPartSet::ActiveDataPartSet(MergeTreeDataFormatVersion format_version_, const Strings & names)
    : format_version(format_version_)
{
    for (const auto & name : names)
        addImpl(name);
}


void ActiveDataPartSet::add(const String & name)
{
    std::lock_guard<std::mutex> lock(mutex);
    addImpl(name);
}


void ActiveDataPartSet::addImpl(const String & name)
{
    auto part_info = MergeTreePartInfo::fromPartName(name, format_version);

    if (!getContainingPartImpl(part_info).empty())
        return;

    /// Parts contained in `part` are located contiguously in `part_info_to_name`, overlapping with the place where the part itself would be inserted.
    auto it = part_info_to_name.lower_bound(part_info);

    /// Let's go left.
    while (it != part_info_to_name.begin())
    {
        --it;
        if (!part_info.contains(it->first))
        {
            ++it;
            break;
        }
        part_info_to_name.erase(it++);
    }

    /// Let's go to the right.
    while (it != part_info_to_name.end() && part_info.contains(it->first))
    {
        part_info_to_name.erase(it++);
    }

    part_info_to_name.emplace(part_info, name);
}


String ActiveDataPartSet::getContainingPart(const String & part_name) const
{
    std::lock_guard<std::mutex> lock(mutex);
    return getContainingPartImpl(MergeTreePartInfo::fromPartName(part_name, format_version));
}


String ActiveDataPartSet::getContainingPartImpl(const MergeTreePartInfo & part_info) const
{
    /// A part can only be covered/overlapped by the previous or next one in `part_info_to_name`.
    auto it = part_info_to_name.lower_bound(part_info);

    if (it != part_info_to_name.end())
    {
        if (it->first.contains(part_info))
            return it->second;
    }

    if (it != part_info_to_name.begin())
    {
        --it;
        if (it->first.contains(part_info))
            return it->second;
    }

    return String();
}


Strings ActiveDataPartSet::getPartsCoveredBy(const MergeTreePartInfo & part_info) const
{
    std::lock_guard lock(mutex);

    auto it_middle = part_info_to_name.lower_bound(part_info);
    auto begin = it_middle;
    while (begin != part_info_to_name.begin())
    {
        auto prev = std::prev(begin);
        if (!part_info.contains(prev->first))
        {
            if (prev->first.contains(part_info))
                return {};

            break;
        }

        begin = prev;
    }

    auto end = it_middle;
    while (end != part_info_to_name.end())
    {
        if (!part_info.contains(end->first))
        {
            if (end->first.contains(part_info))
                return {};
            break;
        }

        ++end;
    }

    Strings covered;
    for (auto it = begin; it != end; ++it)
        covered.push_back(it->second);

    return covered;
}


Strings ActiveDataPartSet::getParts() const
{
    std::lock_guard<std::mutex> lock(mutex);

    Strings res;
    res.reserve(part_info_to_name.size());
    for (const auto & kv : part_info_to_name)
        res.push_back(kv.second);

    return res;
}


size_t ActiveDataPartSet::size() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return part_info_to_name.size();
}


}
