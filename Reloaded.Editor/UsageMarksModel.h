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
    // same key pair up in order), unchecked once it is read-only; one that was not
    // was outside the selection Refresh captured, so it is checked only when
    // replaceable and the scope is off. renumbered: a geometry build since oldRows
    // were listed numbers the BSP surfaces afresh, so an old surface number names
    // another face and every surface row counts as not listed before.
    inline std::vector<Mark> Carry(const Json& oldRows,const std::vector<Mark>& oldMarks,const Json& newRows,bool only,bool renumbered=false)
    {
        std::map<std::string,std::deque<size_t>> listed;
        for(size_t i=0;i<oldRows.size() && i<oldMarks.size();++i)if(!renumbered || oldRows[i].at("surface").get<int>()<0)listed[Key(oldRows[i])].push_back(i);
        std::vector<Mark> marks;marks.reserve(newRows.size());
        for(const auto& row:newRows)
        {
            const bool can=row.at("replaceable").get<bool>();
            auto found=listed.find(Key(row));
            if(found!=listed.end() && !found->second.empty()){auto mark=oldMarks[found->second.front()];found->second.pop_front();mark.checked=mark.checked && can;marks.push_back(mark);}
            else marks.push_back({can && !only,false});
        }
        return marks;
    }
}
