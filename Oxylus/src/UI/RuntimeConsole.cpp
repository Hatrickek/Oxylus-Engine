﻿#include "RuntimeConsole.h"

#include <icons/IconsMaterialDesignIcons.h>

#include "ExternalConsoleSink.h"
#include "Core/Application.h"

#include "Utils/CVars.h"
#include "Utils/StringUtils.h"

namespace Oxylus {
static ImVec4 GetColor(const spdlog::level::level_enum level) {
  switch (level) {
    case spdlog::level::level_enum::info: return {0, 1, 0, 1};
    case spdlog::level::level_enum::warn: return {0.9f, 0.6f, 0.2f, 1};
    case spdlog::level::level_enum::err: return {1, 0, 0, 1};
    case spdlog::level::level_enum::trace: return {1, 1, 1, 1};
    default: return {1, 1, 1, 1};
  }
}

static const char8_t* GetLevelIcon(spdlog::level::level_enum level) {
  switch (level) {
    case spdlog::level::level_enum::trace: return ICON_MDI_MESSAGE_TEXT;
    case spdlog::level::level_enum::debug: return ICON_MDI_BUG;
    case spdlog::level::level_enum::info: return ICON_MDI_INFORMATION;
    case spdlog::level::level_enum::warn: return ICON_MDI_ALERT;
    case spdlog::level::level_enum::err: return ICON_MDI_CLOSE_OCTAGON;
    case spdlog::level::level_enum::critical: return ICON_MDI_ALERT_OCTAGRAM;
    default: ;
  }

  return u8"Unknown name";
}

RuntimeConsole::RuntimeConsole() {
  ExternalConsoleSink::SetConsoleSink_HandleFlush([this](std::string_view message,
                                                         const char*,
                                                         const char*,
                                                         int32_t,
                                                         spdlog::level::level_enum level) {
    add_log(message.data(), level);
  });

  // Default commands
  register_command("quit", "", [] { Application::get()->close(); });
  register_command("clear", "", [this] { clear_log(); });
  register_command("help", "", [this] { help_command(); });

  m_request_scroll_to_bottom = true;
}

void RuntimeConsole::register_command(const std::string& command,
                                     const std::string& on_succes_log,
                                     const std::function<void()>& action) {
  m_command_map.emplace(command, ConsoleCommand{nullptr, nullptr, nullptr, action, on_succes_log});
}

void RuntimeConsole::register_command(const std::string& command, const std::string& on_succes_log, int32_t* value) {
  m_command_map.emplace(command, ConsoleCommand{value, nullptr, nullptr, nullptr, on_succes_log});
}

void RuntimeConsole::register_command(const std::string& command, const std::string& on_succes_log, std::string* value) {
  m_command_map.emplace(command, ConsoleCommand{nullptr, value, nullptr, nullptr, on_succes_log});
}

void RuntimeConsole::register_command(const std::string& command, const std::string& on_succes_log, bool* value) {
  m_command_map.emplace(command, ConsoleCommand{nullptr, nullptr, value, nullptr, on_succes_log});
}

void RuntimeConsole::add_log(const char* fmt, spdlog::level::level_enum level) {
  if ((int32_t)m_text_buffer.size() >= MAX_TEXT_BUFFER_SIZE)
    m_text_buffer.erase(m_text_buffer.begin());
  m_text_buffer.emplace_back(ConsoleText{fmt, level});
  m_request_scroll_to_bottom = true;
}

void RuntimeConsole::clear_log() {
  m_text_buffer.clear();
}

void RuntimeConsole::on_imgui_render(ImGuiWindowFlags window_flags) {
  if (!visible)
    return;
  id = fmt::format(" {} {}\t\t###", StringUtils::from_char8_t(ICON_MDI_CONSOLE), panel_name);
  if (ImGui::Begin(id.c_str(), &visible, window_flags)) {
    if (render_menu_bar) {
      if (ImGui::BeginMenuBar()) {
        if (ImGui::MenuItem(StringUtils::from_char8_t(ICON_MDI_TRASH_CAN))) {
          clear_log();
        }
        ImGui::EndMenuBar();
      }
      ImGui::Separator();
    }

    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ContextMenuInBody |
                                           ImGuiTableFlags_ScrollY;

    float width = 0;
    if (ImGui::BeginChild("TextTable", ImVec2(0, -35))) {
      ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {1, 1});
      if (ImGui::BeginTable("ScrollRegionTable", 1, tableFlags)) {
        width = ImGui::GetWindowSize().x;
        ImGui::PushFont(ImGuiLayer::BoldFont);
        for (auto& text : m_text_buffer) {
          text.render();
        }
        ImGui::PopFont();
        if (m_request_scroll_to_bottom) {
          ImGui::SetScrollY(ImGui::GetScrollMaxY() * 10);
          m_request_scroll_to_bottom = false;
        }
        ImGui::EndTable();
      }
      ImGui::PopStyleVar();
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::PushItemWidth(width - 10);
    constexpr ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue |
                                               ImGuiInputTextFlags_CallbackHistory |
                                               ImGuiInputTextFlags_EscapeClearsAll;
    static char s_InputBuf[256];
    ImGui::PushFont(ImGuiLayer::BoldFont);
    if (set_focus_to_keyboard_always)
      ImGui::SetKeyboardFocusHere();

    auto callback = [](ImGuiInputTextCallbackData* data) {
      const auto panel = (RuntimeConsole*)data->UserData;
      return panel->input_text_callback(data);
    };

    if (ImGui::InputText("##", s_InputBuf,OX_ARRAYSIZE(s_InputBuf), inputFlags, callback, this)) {
      process_command(s_InputBuf);
      m_input_log.emplace_back(s_InputBuf);
      memset(s_InputBuf, 0, sizeof s_InputBuf);
    }

    ImGui::PopFont();
    ImGui::PopItemWidth();
  }
  ImGui::End();
}

void RuntimeConsole::ConsoleText::render() const {
  ImGui::TableNextRow();
  ImGui::TableNextColumn();

  ImGuiTreeNodeFlags flags = 0;
  flags |= ImGuiTreeNodeFlags_OpenOnArrow;
  flags |= ImGuiTreeNodeFlags_SpanFullWidth;
  flags |= ImGuiTreeNodeFlags_FramePadding;
  flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

  ImGui::PushID(text.c_str());
  ImGui::PushStyleColor(ImGuiCol_Text, GetColor(level));
  const auto levelIcon = GetLevelIcon(level);
  ImGui::TreeNodeEx(text.c_str(), flags, "%s  %s", StringUtils::from_char8_t(levelIcon), text.c_str());
  ImGui::PopStyleColor();

  if (ImGui::BeginPopupContextItem("Popup")) {
    if (ImGui::MenuItem("Copy"))
      ImGui::SetClipboardText(text.c_str());

    ImGui::EndPopup();
  }
  ImGui::PopID();
}

template <typename T>
T get_current_cvar_value(uint32_t cvar_array_index) {
  auto current_value = CVarSystem::get()->get_cvar_array<T>()->cvars[cvar_array_index].current;

  return current_value;
}

template <typename T>
void log_cvar_change(RuntimeConsole* console, const char* cvar_name, T current_value, bool changed) {
  const std::string log_text = changed
                                 ? fmt::format("Changed {} to {}", cvar_name, current_value)
                                 : fmt::format("{} {}", cvar_name, current_value);
  const LevelEnum log_level = changed ? LevelEnum::info : LevelEnum::trace;
  console->add_log(log_text.c_str(), log_level);
}

void RuntimeConsole::process_command(const std::string& command) {
  const auto parsed_command = parse_command(command);
  const auto value = parse_value(command);

  bool is_cvar_variable = false;

  const auto cvar = CVarSystem::get()->get_cvar(parsed_command.c_str());
  if (cvar) {
    is_cvar_variable = true;
    switch (cvar->type) {
      case CVarType::INT: {
        auto current_value = get_current_cvar_value<int32_t>(cvar->arrayIndex);
        bool changed = false;
        if (!value.str_value.empty()) {
          const auto parsed = value.as<int32_t>();
          if (parsed.has_value()) {
            CVarSystem::get()->set_int_cvar(cvar->name.c_str(), *parsed);
            current_value = *parsed;
            changed = true;
          }
        }
        log_cvar_change<int32_t>(this, cvar->name.c_str(), current_value, changed);
        break;
      }
      case CVarType::FLOAT: {
        auto current_value = get_current_cvar_value<float>(cvar->arrayIndex);
        bool changed = false;
        if (!value.str_value.empty()) {
          const auto parsed = value.as<float>();
          if (parsed.has_value()) {
            CVarSystem::get()->set_float_cvar(cvar->name.c_str(), *parsed);
            current_value = *parsed;
            changed = true;
          }
        }
        log_cvar_change<float>(this, cvar->name.c_str(), current_value, changed);
        break;
      }
      case CVarType::STRING: {
        auto current_value = get_current_cvar_value<std::string>(cvar->arrayIndex);
        bool changed = false;
        if (!value.str_value.empty()) {
          CVarSystem::get()->set_string_cvar(cvar->name.c_str(), value.str_value.c_str());
            current_value = value.str_value;
            changed = true;
        }
        log_cvar_change<std::string>(this, cvar->name.c_str(), current_value, changed);
        break;
      }
    }
  }

  // commands registered with register_command()
  if (m_command_map.contains(parsed_command)) {
    const auto& c = m_command_map[parsed_command];
    if (c.action != nullptr) {
      c.action();
    }
    if (!value.str_value.empty()) {
      if (c.str_value != nullptr) {
        *c.str_value = value.str_value;
      }
      else if (c.int_value != nullptr) {
        const auto v = value.as<int32_t>();
        if (v.has_value()) {
          *c.int_value = v.value();
        }
      }
      else if (c.bool_value != nullptr) {
        const auto v = value.as<int32_t>();
        if (v.has_value()) {
          *c.bool_value = v.value();
        }
      }
    }
    if (!c.on_succes_log.empty())
      add_log(c.on_succes_log.c_str(), LevelEnum::info);
  }
  else {
    if (!is_cvar_variable)
      add_log("Non existent command.", LevelEnum::err);
  }
}

RuntimeConsole::ParsedCommandValue RuntimeConsole::parse_value(const std::string& command) {
  if (command.find(' ') == std::string::npos)
    return {""};
  const auto offset = command.find(' ');
  const auto value = command.substr(offset + 1, command.size() - offset);
  return {value};
}

std::string RuntimeConsole::parse_command(const std::string& command) {
  return command.substr(0, command.find(' '));
}

int RuntimeConsole::input_text_callback(ImGuiInputTextCallbackData* data) {
  if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
    const int prevHistoryPos = m_history_position;
    if (data->EventKey == ImGuiKey_UpArrow) {
      if (m_history_position == -1)
        m_history_position = (int32_t)m_input_log.size() - 1;
      else if (m_history_position > 0)
        m_history_position--;
    }
    else if (data->EventKey == ImGuiKey_DownArrow) {
      if (m_history_position != -1)
        if (++m_history_position >= (int32_t)m_input_log.size())
          m_history_position = -1;
    }

    if (prevHistoryPos != m_history_position) {
      const char* historyStr = m_history_position >= 0 ? m_input_log[m_history_position] : "";
      data->DeleteChars(0, data->BufTextLen);
      data->InsertChars(0, historyStr);
    }
  }

  return 0;
}

void RuntimeConsole::help_command() {
  std::string availableCommands = "Available commands: \n";
  for (auto& [commandStr, command] : m_command_map) {
    availableCommands.append(fmt::format("\t{0} \n", commandStr));
  }

  for (int i = 0; i < CVarSystem::get()->get_cvar_array<int32_t>()->lastCVar; i++) {
    const auto p = CVarSystem::get()->get_cvar_array<int32_t>()->cvars[i].parameter;
    availableCommands.append(fmt::format("\t{0} \n", p->name.c_str()));
  }

  for (int i = 0; i < CVarSystem::get()->get_cvar_array<float>()->lastCVar; i++) {
    const auto p = CVarSystem::get()->get_cvar_array<int32_t>()->cvars[i].parameter;
    availableCommands.append(fmt::format("\t{0} \n", p->name.c_str()));
  }

  add_log(availableCommands.c_str(), LevelEnum::trace);
}
}
