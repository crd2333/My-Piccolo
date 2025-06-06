#include <filesystem>

#include "runtime/resource/asset_manager/asset_manager.h"

#include "runtime/function/framework/component/lua/lua_component.h"
#include "runtime/core/base/macro.h"
#include "runtime/function/framework/object/object.h"
namespace Piccolo {
bool find_component_field(std::weak_ptr<GObject>     game_object,
                          const char*                field_name,
                          Reflection::FieldAccessor &field_accessor,
                          void*                     &target_instance) {
    auto components = game_object.lock()->getComponents();

    std::istringstream iss(field_name); // e.g. "Transform.Position.X"
    std::string        current_name;
    std::getline(iss, current_name, '.'); // get first component name, e.g. "Transform"
    auto component_iter = std::find_if(
    components.begin(), components.end(), [current_name](auto c) { return c.getTypeName() == current_name; });
    if (component_iter != components.end()) {
        auto meta = Reflection::TypeMeta::newMetaFromName(current_name);
        void* field_instance = component_iter->getPtr();

        // find target field
        while (std::getline(iss, current_name, '.')) {
            // Reflection::FieldAccessor* fields;
            std::vector<Reflection::FieldAccessor> fields;
            int fields_count = meta.getFieldsList(fields);
            auto field_iter   = std::find_if(
            fields.begin(), fields.end(), [current_name](auto f) { return f.getFieldName() == current_name; });
            if (field_iter == fields.end()) // not found
                return false;

            field_accessor = *field_iter;

            target_instance = field_instance;

            // for next iteration
            field_instance = field_accessor.get(target_instance);
            field_accessor.getTypeMeta(meta);
        }
        return true;
    }
    return false;
}

template<typename T>
void LuaComponent::set(std::weak_ptr<GObject> game_object, const char* name, T value) {
    // LOG_INFO(name);
    Reflection::FieldAccessor field_accessor;
    void*                     target_instance;
    if (find_component_field(game_object, name, field_accessor, target_instance))
        field_accessor.set(target_instance, &value);
    else
        LOG_ERROR("Can't find target field.");
}

template<typename T>
T LuaComponent::get(std::weak_ptr<GObject> game_object, const char* name) {
    // LOG_INFO(name);

    Reflection::FieldAccessor field_accessor;
    void*                     target_instance;
    if (find_component_field(game_object, name, field_accessor, target_instance))
        return *(T * )field_accessor.get(target_instance);
    else
        LOG_ERROR("Can't find target field.");
}

void LuaComponent::invoke(std::weak_ptr<GObject> game_object, const char* name) {
    // LOG_INFO(name);

    Reflection::TypeMeta meta;
    void*                target_instance = nullptr;
    std::string          method_name;

    // get target instance and meta
    std::string target_name(name);
    size_t  pos = target_name.find_last_of('.');
    method_name = target_name.substr(pos + 1, target_name.size());
    target_name = target_name.substr(0, pos);

    if (target_name.find_first_of('.') == target_name.npos) { // target is a component
        auto components = game_object.lock()->getComponents();

        auto component_iter = std::find_if(
        components.begin(), components.end(), [target_name](auto c) { return c.getTypeName() == target_name; });
        if (component_iter != components.end()) {
            meta = Reflection::TypeMeta::newMetaFromName(target_name);
            target_instance = component_iter->getPtr();
        } else {
            LOG_ERROR("Cand find component");
            return;
        }
    } else { // target is a field of a component
        Reflection::FieldAccessor field_accessor;
        if (find_component_field(game_object, name, field_accessor, target_instance)) {
            target_instance = field_accessor.get(target_instance);
            field_accessor.getTypeMeta(meta);
        } else {
            LOG_ERROR("Can't find target field.");
            return;
        }
    }

    // invoke function
    std::vector<Reflection::MethodAccessor> methods;
    size_t method_count = meta.getMethodsList(methods);
    auto method_iter = std::find_if(
    methods.begin(), methods.end(), [method_name](auto m) { return m.getMethodName() == method_name; });
    if (method_iter != methods.end())
        method_iter->invoke(target_instance);
    else
        LOG_ERROR("Cand find method");
}

void LuaComponent::postLoadResource(std::weak_ptr<GObject> parent_object) {
    m_parent_object = parent_object;
    m_lua_state.open_libraries(sol::lib::base);
    m_lua_state.set_function("set_float", &LuaComponent::set<float>);
    m_lua_state.set_function("get_bool", &LuaComponent::get<bool>);
    m_lua_state.set_function("invoke", &LuaComponent::invoke);
    m_lua_state["GameObject"] = m_parent_object;

    loadLuaScript(); // load lua script from file or string
}

void LuaComponent::tick(float delta_time) {
    // LOG_INFO(m_lua_script);
    m_lua_state.script(m_lua_script);
}

void LuaComponent::loadLuaScript() {
    if (m_lua_script.empty()) {
        LOG_WARN("Lua script is empty");
        return;
    }

    if (isLuaFilePath(m_lua_script)) {
        // 是文件路径，尝试加载文件
        std::string script_content = loadLuaScriptFromFile(m_lua_script);
        if (!script_content.empty()) {
            m_lua_script = script_content;
            LOG_INFO("Loaded Lua script from file:\n{}", m_lua_script);
        } else
            LOG_ERROR("Failed to load Lua script from file:\n{}", m_lua_script);
    }
    // 如果不是文件路径，则直接使用 m_lua_script 作为硬编码脚本
}

bool LuaComponent::isLuaFilePath(const std::string& script) const {
    // 1. 以 .lua 结尾
    if (script.size() > 4 && script.substr(script.size() - 4) == ".lua") {
        return true;
    }

    // 2. 包含路径分隔符但不包含 Lua 关键字
    if ((script.find('/') != std::string::npos || script.find('\\') != std::string::npos) &&
        script.find("function") == std::string::npos &&
        script.find("local") == std::string::npos &&
        script.find("print") == std::string::npos) {
        return true;
    }

    // 3. 为不包含 Lua 语法字符的简短文件名
    if (script.find('\n') == std::string::npos &&
        script.find(';') == std::string::npos &&
        script.find('(') == std::string::npos &&
        script.find('{') == std::string::npos &&
        script.length() < 256) {
        return true;
    }

    return false;
}

std::string LuaComponent::loadLuaScriptFromFile(const std::string& file_path) {
    std::shared_ptr<AssetManager> asset_manager = g_runtime_global_context.m_asset_manager;
    ASSERT(asset_manager);

    try {
        std::filesystem::path full_path = asset_manager->getFullPath(file_path);

        if (!std::filesystem::exists(full_path)) {
            LOG_ERROR("Lua script file does not exist: {}", full_path.string());
            return "";
        }
        std::ifstream file(full_path);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open Lua script file: {}", full_path.string());
            return "";
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        file.close();

        return content;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception while loading Lua script file {}: {}", file_path, e.what());
        return "";
    }
}

} // namespace Piccolo
