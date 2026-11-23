#pragma once
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>

namespace VrModLauncher {
enum class Kind {Text,File,Choice,Boolean,Note,Hidden};
struct Field {
    std::wstring id,label,help,value;
    Kind kind=Kind::Text;
    std::vector<std::wstring> choices;
    bool enabled=true;
};
struct Page {std::wstring id,label;std::vector<Field> fields;};
struct Model {
    std::vector<Page> pages;
    Field& Get(const std::wstring& id) {
        for(auto& page:pages)for(auto& field:page.fields)if(field.id==id)return field;
        throw std::out_of_range("Unknown launcher field");
    }
    const Field& Get(const std::wstring& id) const {
        for(const auto& page:pages)for(const auto& field:page.fields)if(field.id==id)return field;
        throw std::out_of_range("Unknown launcher field");
    }
};
struct Action {std::wstring label;std::function<void(const Model&)> run;};
struct App {
    std::wstring id,title,heading,description;
    Model model;
    std::function<bool(Model&,std::wstring&)> save;
    std::function<void(Model&)> changed;
    std::function<bool(Model&,std::wstring&)> validate;
    std::function<bool(const Model&,std::wstring&)> inspect;
    std::function<bool(const Model&,std::wstring&,std::function<void(const std::wstring&)>)> launch;
    std::function<std::wstring()> discover;
    std::vector<Action> actions;
    std::wstring supportLabel;
    std::function<bool(const Model&,const std::wstring& outputFolder,std::wstring&)> support;
};
// IDs are global to the model: adapters use them across page boundaries.
// Structural validation is independent of game-specific path/runtime validation.
inline bool ValidateModel(const Model& model,std::wstring& error) {
    if(model.pages.empty()){error=L"The launcher has no settings pages.";return false;}
    std::set<std::wstring> pages,fields;
    for(const auto& page:model.pages){
        if(page.id.empty()||!pages.insert(page.id).second){error=L"Empty or duplicate page ID: "+page.id;return false;}
        for(const auto& field:page.fields){
            if(field.id.empty()||!fields.insert(field.id).second){error=L"Empty or duplicate field ID: "+field.id;return false;}
            if(field.kind==Kind::Boolean&&field.value!=L"0"&&field.value!=L"1"){
                error=L"Boolean field must contain 0 or 1: "+field.id;return false;
            }
            if(field.kind==Kind::Choice&&(field.choices.empty()||
               std::find(field.choices.begin(),field.choices.end(),field.value)==field.choices.end())){
                error=L"Choice field has no selected option: "+field.id;return false;
            }
        }
    }
    error.clear();return true;
}
}
