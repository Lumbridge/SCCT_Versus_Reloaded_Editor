#pragma once
// Find Usages' check boxes. A usage row is {actor (an identity, or null for a
// BSP surface no actor owns), property, surface (-1 for an actor property),
// replaceable, selected}. Refresh marks the rows from the selection as it is
// then; when an edit elsewhere lists the usages again, each row keeps the
// marks it had, so what was unchecked by hand stays unchecked.
#include "WorkflowModel.h"
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace Workflow::UsageMarks
{
    // The same usage across two listings: the actor, the property and the surface.
    inline std::string Key(const Json& row)
    {
        const auto& actor=row.at("actor");
        return Fold(actor.is_object()?actor.value("path",std::string{}):std::string("BSP"))+"|"+row.at("property").get<std::string>()+"|"+std::to_string(row.at("surface").get<int>());
    }
    // checked: the row's check box. scoped: its actor was selected when Refresh
    // ran, which is what "Limit to selection captured at Refresh" keeps to.
    struct Mark { bool checked=false,scoped=false; };
    inline Mark Default(const Json& row,bool only)
    {
        const bool selected=row.at("selected").get<bool>();
        return {row.at("replaceable").get<bool>() && (!only || selected),selected};
    }
    // The marks for newRows: a usage listed before keeps its marks (rows with the
    // same key pair up in order); one that was not was outside the selection Refresh
    // captured, so it is checked only when replaceable and the scope is off.
    inline std::vector<Mark> Carry(const Json& oldRows,const std::vector<Mark>& oldMarks,const Json& newRows,bool only)
    {
        std::map<std::string,std::deque<size_t>> listed;
        for(size_t i=0;i<oldRows.size() && i<oldMarks.size();++i)listed[Key(oldRows[i])].push_back(i);
        std::vector<Mark> marks;marks.reserve(newRows.size());
        for(const auto& row:newRows)
        {
            auto found=listed.find(Key(row));
            if(found!=listed.end() && !found->second.empty()){marks.push_back(oldMarks[found->second.front()]);found->second.pop_front();}
            else marks.push_back({row.at("replaceable").get<bool>() && !only,false});
        }
        return marks;
    }
}
