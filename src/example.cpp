#include <iostream>
#include "excmd.h"

// Options for Decaf ;)

int main(int argc, char **argv)
{
   excmd::parser parser;
   using excmd::description;
   using excmd::optional;
   using excmd::default_value;
   using excmd::allowed;
   using excmd::value;

   try {
      parser.global_options()
         .add_option("v,version", description { "Show version." })
         .add_option("h,help", description { "Show help." });

      parser.add_command("help")
         .add_argument("help-command", optional {}, value<std::string> {});

      auto jit_options = parser.add_option_group("JIT Options")
         .add_option("jit", description { "Enables the JIT engine." })
         .add_option("jit-debug", description { "Verify JIT implementation against interpreter." });

      auto log_options = parser.add_option_group("Log Options")
         .add_option("log-file", description { "Redirect log output to file." })
         .add_option("log-async", description { "Enable asynchronous logging." })
         .add_option("log-level", description { "Only display logs with severity equal to or greater than this level." },
                     default_value<std::string> { "trace" },
                     allowed<std::string> { {
                        "trace", "debug", "info", "notice", "warning", "error", "critical", "alert", "emerg", "off"
                     } });

      auto sys_options = parser.add_option_group("System Options")
         .add_option("sys-path", description { "Where to locate any external system files." }, value<std::string> {});

      parser.add_command("play")
         .add_option_group(jit_options)
         .add_option_group(log_options)
         .add_option_group(sys_options)
         .add_argument("game directory", value<std::string> {});

      parser.add_command("fuzztest");

      parser.add_command("hwtest")
         .add_option_group(jit_options)
         .add_option_group(log_options);

      auto options = parser.parse(argc, argv);

      if (options.has("sys-path")) {
         auto path = options.get<std::string>("sys-path");
         std::cout << "sys-path: " << path << std::endl;
      }

      if (options.has("play")) {
         auto path = options.get<std::string>("game directory");
         std::cout << "play game dir: " << path << std::endl;
      } else if (options.has("hwtest")) {
         std::cout << "hwtest" << std::endl;
      } else if (options.has("fuzztest")) {
         std::cout << "fuzztest" << std::endl;
      }

      if (options.has("version")) {
         std::cout << "Decaf Emulator version 0.0.1" << std::endl;
         std::exit(0);
      }

      if (argc == 1 || options.has("help")) {
         if (options.has("help-command")) {
            std::cout << parser.format_help(argv[0], options.get<std::string>("help-command")) << std::endl;
         } else {
            std::cout << parser.format_help(argv[0]) << std::endl;
         }

         std::exit(0);
      }
   } catch (excmd::exception ex) {
      std::cout << "Error parsing options: " << ex.what() << std::endl;
      std::exit(-1);
   }

   return 0;
}
