#pragma once
// Find Usages' check boxes. A usage row is {actor (an identity, or null for a
// BSP surface no actor owns), property, surface (-1 for an actor property),
// replaceable, selected}. Refresh marks the rows from the selection as it is
// then; when an edit elsewhere lists the usages again, each row keeps the
// marks it had, so what was unchecked by hand stays unchecked, also when an
// edit takes it off the list for a while.
#include "WorkflowModel.h"
#include <deque>
#include <map>
#include <set>
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
    // excluded: the user left it out, unchecking it while it could be replaced.
    // It is remembered while the row is read-only, so the row does not come
    // back checked when it can be replaced again; checking it takes it back.
    struct Mark { bool checked=false,scoped=false,excluded=false; };
    inline Mark Default(const Json& row,bool only)
    {
        const bool selected=row.at("selected").get<bool>();
        return {row.at("replaceable").get<bool>() && (!only || selected),selected};
    }
    // The marks for newRows: a usage listed before keeps its marks (rows with the
    // same key pair up in order), unchecked once it is read-only, and marked as
    // Refresh would have marked it from its captured scope once it is replaceable
    // where it was not, unless the user had left it out; one that was not listed
    // was outside the selection Refresh captured, so it is checked only when
    // replaceable and the scope is off.
    // renumbered: a geometry build since oldRows were listed numbers the BSP
    // surfaces afresh, so an old surface number names another face and every
    // surface row counts as not listed before. If one of the old surfaces was
    // left out by hand, no surface row comes in checked, and each counts as left
    // out, so a read-only one does not come back checked once it can be
    // replaced: which face it is now cannot be told, so the user checks them
    // again.
    // away: the keys of usages left out by hand that are not listed, because an
    // edit took them off the list or their actor was deleted (one entry per
    // row). One that comes back, for example after Undo, is still left out. A
    // build drops the surfaces among them, and they count as left out as above.
    inline std::vector<Mark> Carry(const Json& oldRows,const std::vector<Mark>& oldMarks,const Json& newRows,bool only,bool renumbered=false,std::multiset<std::string>* away=nullptr)
    {
        // Left out by hand: unchecked while it could be replaced, or excluded
        // then and not checked since (only the user checks a read-only row).
        auto leftOut=[&](size_t i){ return !oldMarks[i].checked && (oldRows[i].at("replaceable").get<bool>() || oldMarks[i].excluded); };
        auto surface=[](const Json& row){ return row.at("surface").get<int>()>=0; };
        std::multiset<std::string> none; auto& gone=away?*away:none;
        std::map<std::string,std::deque<size_t>> listed; bool keptOut=false;
        // An actor property's key ends in surface -1.
        if(renumbered) for(auto key=gone.begin();key!=gone.end();) if(key->ends_with("|-1")) ++key; else { keptOut=true; key=gone.erase(key); }
        for(size_t i=0;i<oldRows.size() && i<oldMarks.size();++i)
        {
            if(!renumbered || !surface(oldRows[i])) listed[Key(oldRows[i])].push_back(i);
            else keptOut=keptOut || leftOut(i);
        }
        std::vector<Mark> marks;marks.reserve(newRows.size());
        for(const auto& row:newRows)
        {
            const bool can=row.at("replaceable").get<bool>(); const auto key=Key(row);
            auto found=listed.find(key);
            if(found!=listed.end() && !found->second.empty())
            {
                const auto old=found->second.front();found->second.pop_front();auto mark=oldMarks[old];
                mark.excluded=leftOut(old);
                if(can && !oldRows[old].at("replaceable").get<bool>()) mark.checked=!mark.excluded && (!only || mark.scoped);
                else mark.checked=mark.checked && can;
                marks.push_back(mark);
            }
            else if(auto back=gone.find(key);back!=gone.end()) { gone.erase(back); marks.push_back({false,false,true}); }
            else { const bool out=keptOut && surface(row); marks.push_back({can && !only && !out,false,out}); }
        }
        // The old rows left out by hand that no new row paired with are away.
        for(const auto& [key,rows]:listed) for(auto old:rows) if(leftOut(old)) gone.insert(key);
        return marks;
    }
}
