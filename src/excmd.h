#pragma once
#include <regex>
#include <map>
#include <sstream>
#include "excmd_exception.h"
#include "excmd_meta.h"
#include "excmd_value_parser.h"

namespace excmd
{

struct option
{
   bool requires_value()
   {
      return parser != nullptr;
   }

   bool get_value(void *ptr, size_t size)
   {
      return parser->get_value(ptr, size);
   }

   bool get_default_value(std::string &str)
   {
      if (!parser) {
         return false;
      } else {
         return parser->get_default_value(str);
      }
   }

   bool optional;
   std::string name;
   std::string shortName;
   std::string longName;
   std::string description;
   value_parser *parser;
};

struct option_group
{
   std::string name;
   std::vector<option *> options;
};

struct command
{
   std::string name;
   std::vector<option *> arguments;
   std::vector<option_group *> groups; // non-owning
};

struct option_group_adder
{
   template<typename... Types>
   option_group_adder &add_option(const std::string &name, Types... args)
   {
      auto opt = new option {};
      opt->description = get_description<Types...>::get(args...);
      opt->parser = get_value_parser<Types...>::get(args...);

      auto cpos = name.find_first_of(',');
      if (cpos != std::string::npos) {
         auto left = name.substr(0, cpos);
         auto right = name.substr(cpos + 1);

         if (left.size() == 1) {
            opt->shortName = left;
            opt->longName = right;
         } else if (right.size() == 1) {
            opt->shortName = right;
            opt->longName = left;
         } else {
            throw invalid_option_name_exception(name);
         }
      } else if (name.size() == 1) {
         opt->shortName = name;
      } else {
         opt->longName = name;
      }

      opt->name = opt->longName.empty() ? opt->shortName : opt->longName;
      group->options.push_back(opt);
      return *this;
   }

   option_group *group;
};

struct command_adder
{
   command_adder &add_option_group(option_group *group)
   {
      cmd->groups.push_back(group);
      return *this;
   }

   command_adder &add_option_group(const option_group_adder &adder)
   {
      return add_option_group(adder.group);
   }

   template<typename... Types>
   command_adder &add_argument(const std::string &name, Types... args)
   {
      auto opt = new option {};
      opt->name = name;
      opt->description = get_description<Types...>::get(args...);
      opt->parser = get_value_parser<Types...>::get(args...);
      opt->optional = get_optional<Types...>::value;
      cmd->arguments.push_back(opt);
      return *this;
   }

   command *cmd;
};

struct option_state
{
   bool has(const std::string &name)
   {
      if (cmd && cmd->name == name) {
         return true;
      } else {
         return set_options.find(name) != set_options.end();
      }
   }

   template<typename Type>
   Type get(const std::string &name)
   {
      Type type {};
      auto itr = set_options.find(name);

      if (itr == set_options.end()) {
         return type;
      } else {
         itr->second->get_value(&type, sizeof(Type));
         return type;
      }
   }

   std::size_t args_set = 0;
   command *cmd = nullptr;
   std::map<std::string, option *> set_options;
   std::vector<std::string> extra_arguments;
};

struct parser
{
   option_group global = { "Global Options", {} };
   std::vector<option_group *> groups;
   std::vector<command *> commands;

   option_group_adder add_option_group(const std::string &name)
   {
      auto group = new option_group {};
      group->name = name;
      groups.push_back(group);
      return option_group_adder { group };
   }

   option_group_adder global_options()
   {
      return option_group_adder { &global };
   }

   command_adder add_command(const std::string &name)
   {
      auto cmd = new command {};
      cmd->name = name;
      commands.push_back(cmd);
      return command_adder { cmd };
   }

   bool is_valid_value(int argc, char **argv, int i)
   {
      if (i >= argc) {
         return false;
      }

      if (argv[i][0] == '-') {
         return false;
      }

      return true;
   }

   bool set_option(option_state &state, option *opt, const std::string &value = {})
   {
      if (opt->parser && !opt->parser->parse(value)) {
         return false;
      }

      state.set_options[opt->name] = opt;
      return true;
   }

   option_state parse(int argc, char **argv)
   {
      option_state state;
      auto option_matcher = std::basic_regex<char> { "--([[:alnum:]][-_[:alnum:]]+)(=(.*))?|-([a-zA-Z]+)" };
      auto pos = 1;

      while (pos != argc) {
         std::match_results<const char*> result;
         std::regex_match(argv[pos], result, option_matcher);

         if (result.empty()) {
            auto positional = argv[pos];

            if (!state.cmd) {
               state.cmd = find_command(positional);

               if (!state.cmd && commands.size()) {
                  throw option_not_exists_exception(positional);
               }
            } else if (commands.size() == 0 || state.args_set >= state.cmd->arguments.size()) {
               state.extra_arguments.push_back(positional);
            } else {
               auto arg = state.cmd->arguments[state.args_set++];
               set_option(state, arg, positional);
            }
         } else if (result[4].length()) {
            // Short option(s)
            // -sfoo or -abcdef
            auto short_options = result[4].str();

            for (auto i = 0u; i < short_options.size(); ++i) {
               auto name = short_options.substr(i, 1);
               auto opt = find_option(name, state.cmd);

               if (!opt) {
                  throw option_not_exists_exception(name);
               }

               if (!opt->requires_value()) {
                  set_option(state, opt);
               } else if (i == short_options.size() - 1) {
                  // -s value
                  if (!is_valid_value(argc, argv, pos + 1)) {
                     throw missing_value_exception(opt->name);
                  }

                  set_option(state, opt, argv[pos + 1]);
                  pos++;
               } else if (i == 0) {
                  // -svalue
                  auto value = short_options.substr(i + 1);
                  set_option(state, opt, value);
                  break;
               } else {
                  // -abcvalue is not valid syntax
                  throw missing_value_exception(opt->name);
               }
            }
         } else if (result[1].length()) {
            // Long option
            auto name = result[1].str();
            auto opt = find_option(name, state.cmd);

            if (!opt) {
               throw option_not_exists_exception(name);
            }

            if (result[3].length()) {
               // --long=value
               if (!opt->requires_value()) {
                  throw not_expecting_value_exception(opt->name);
               }

               auto value = result[3].str();
               set_option(state, opt, value);
            } else {
               if (!opt->requires_value()) {
                  // --long
                  set_option(state, opt);
               } else {
                  // --long value
                  if (!is_valid_value(argc, argv, pos + 1)) {
                     throw missing_value_exception(opt->name);
                  }

                  set_option(state, opt, argv[pos + 1]);
                  pos++;
               }
            }
         }

         ++pos;
      }

      // Check that we have read all required arguments for a command
      if (state.cmd && state.args_set < state.cmd->arguments.size()) {
         for (auto i = state.args_set; i < state.cmd->arguments.size(); ++i) {
            if (!state.cmd->arguments[i]->optional) {
               throw command_missing_argument_exception(state.cmd->name, state.cmd->arguments[i]->name);
            }
         }
      }

      return state;
   }

   command *find_command(const std::string &name)
   {
      for (auto command : commands) {
         if (command->name == name) {
            return command;
         }
      }

      return nullptr;
   }

   option *find_option(const std::string &name, command *activeCommand)
   {
      auto option = find_option(name, &global);

      if (option) {
         return option;
      }

      if (activeCommand) {
         for (auto group : activeCommand->groups) {
            auto option = find_option(name, group);

            if (option) {
               return option;
            }
         }
      }

      return nullptr;
   }

   option *find_option(const std::string &name, option_group *group)
   {
      for (auto option : group->options) {
         if (option->shortName == name || option->longName == name) {
            return option;
         }
      }

      return nullptr;
   }

   std::string format_option_group(option_group *group)
   {
      std::ostringstream os;
      os << group->name << ":" << std::endl;

      for (auto option : group->options) {
         std::string default_value;
         os << "  ";

         if (option->shortName.size()) {
            os << "-" << option->shortName << " ";
         }

         if (option->longName.size()) {
            os << "--" << option->longName;
         }

         if (option->requires_value()) {
            os << "=<" << (option->longName.empty() ? option->shortName : option->longName) << ">";
         }

         if (option->get_default_value(default_value)) {
            os << " [default=" << default_value << "]";
         }

         os << std::endl;
         os << "    " << option->description << std::endl;
      }

      return os.str();
   }

   std::string format_command(command *cmd)
   {
      std::ostringstream os;
      os << cmd->name;

      for (auto group : cmd->groups) {
         for (auto option : group->options) {
            os << " [";

            if (option->name.length() == 1) {
               os << "-";
            } else {
               os << "--";
            }

            os << option->name;

            if (option->requires_value()) {
               os << "=<" << option->name << ">";
            }

            os << "]";
         }
      }

      for (auto argument : cmd->arguments) {
         os << " <" << argument->name << ">";
      }

      return os.str();
   }

   std::string format_help(const std::string &name)
   {
      std::ostringstream os;

      // Print commands
      if (commands.size()) {
         os << "Usage:" << std::endl;

         for (auto cmd : commands) {
            os << "  " << name << " " << format_command(cmd) << std::endl;
         }
      }

      // Print global options
      os << format_option_group(&global) << std::endl;

      // Print every option group
      for (auto group : groups) {
         os << format_option_group(group) << std::endl;
      }

      return os.str();
   }

   std::string format_help(const std::string &name, const std::string &cmd_name)
   {
      std::ostringstream os;
      auto cmd = find_command(cmd_name);

      // If command doesn't exist print the full help
      if (!cmd) {
         os << "Command " << cmd_name << " not found." << std::endl;
         os << format_help(name);
         return os.str();
      }

      // Print just the selected command
      os << "Usage:" << std::endl;
      os << "  " << name << " " << format_command(cmd) << std::endl;

      // Print global options
      os << format_option_group(&global) << std::endl;

      // Print just the selected command's options
      for (auto group : cmd->groups) {
         os << format_option_group(group) << std::endl;
      }

      return os.str();
   }
};

} // namespace excmd
