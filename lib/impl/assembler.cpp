#include "assembler.h"

#include <regex>


namespace
{
    /// trim from start (in place)
    void ltrim(std::string& s)
    {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch)
        {
            return !std::isspace(ch);
        }));
    }

    /// trim from end (in place)
    void rtrim(std::string& s)
    {
        s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch)
        {
            return !std::isspace(ch);
        }).base(), s.end());
    }

    /// trim from both ends (in place)
    void trim(std::string& s)
    {
        ltrim(s);
        rtrim(s);
    }

    /// Split s at every delim into a vector of string
    std::vector<std::string> split(const std::string& s, char delim)
    {
        std::vector<std::string> elems;
        std::stringstream ss(s);
        std::string item;

        while (std::getline(ss, item, delim))
        {
            elems.push_back(item);
        }

        return elems;
    }
}

namespace Assembler
{
    void preprocess(std::istream& input, std::ostream& output)
    {
        for(std::string line; getline(input, line);)
        {
            using namespace Direction;

            line = std::regex_replace(line, std::regex("BEFORE"), std::to_string(BEFORE));
            line = std::regex_replace(line, std::regex("CURRENT"), std::to_string(CURRENT));
            line = std::regex_replace(line, std::regex("AFTER"), std::to_string(AFTER));
            line = std::regex_replace(line, std::regex("PC"), std::to_string(static_cast<int>(SpecialDirection::PC)));
            line = std::regex_replace(line, std::regex("MEMBANK"), std::to_string(static_cast<int>(SpecialDirection::MEMBANK)));

            output << line << std::endl;
        }
    }

    std::tuple<Ast, SettingMap, std::vector<std::vector<std::pair<BlockId, int>>>> build_ast(std::istream& input)
    {
        throw_cpu_exception_if(input, "Invalid input");

        // Regex
        std::regex find_setting(R"(\.(\w+) ([\d, ]*))");
        std::regex find_block(R"((\d+):)");
        std::regex find_instruction(R"((\w{3})( -?\d+(?:, ?\d+)*)?)");
        std::regex find_load_with_arg(R"((LC[LH]) ([a-z]))");

        // Result
        Ast blocks;
        SettingMap settings;
        std::vector<std::vector<std::pair<BlockId, int>>> variables;

        // State machine
        auto setting_done = false;
        auto current_block = std::optional<int>();
        std::smatch base_match;

        for (auto [line, line_number] = std::make_pair(std::string(), 1); getline(input, line); line_number++)
        {
            trim(line);

            if (line.empty() || line.at(0) == ';')
            {
                continue;
            }
            else if (!setting_done && std::regex_match(line, base_match, find_setting))
            {
                assert(base_match.size() == 3);

                auto name = base_match[1];
                auto args = split(base_match[2], ',');

                std::vector<uint8_t> args_int;
                std::transform(std::cbegin(args), std::cend(args), std::back_inserter(args_int), [&line_number](auto& arg)
                {
                    auto arg_int = std::stol(arg);
                    throw_cpu_exception_if(arg_int <= 0xff, "Too large setting at line " << line_number);
                    assert(arg_int >= 0);
                    return arg_int;
                });

                settings.emplace(name, args_int);
            }
            else if (current_block && std::regex_match(line, base_match, find_load_with_arg))
            {
                assert(base_match.size() == 3);  // All + OPCode + arg

                auto instruction = base_match[1];
                auto var = base_match[2].str()[0] - 'a';    // Check distance between the first possible argument

                assert(var >= 0 && var < 26);

                blocks[*current_block].emplace_back(instruction, std::vector({0_u8}));
                if (variables.size() < var + 1u)
                {
                    variables.resize(var + 1u);
                }

                variables.at(var).emplace_back(*current_block, blocks[*current_block].size() - 1);
            }
            else if (current_block && std::regex_match(line, base_match, find_instruction))
            {
                assert(base_match.size() == 3);  // All + OPCode + args

                auto instruction = base_match[1];
                auto args = split(base_match[2], ',');

                std::vector<uint8_t> args_int;
                std::transform(std::cbegin(args), std::cend(args), std::back_inserter(args_int), [&line_number](auto& arg)
                {
                    auto arg_int = std::stol(arg);
                    throw_cpu_exception_if(arg_int < 0xff, "Too large argument at line " << line_number);
                    assert(arg_int >= 0);
                    return arg_int;
                });

                blocks[*current_block].emplace_back(instruction, args_int);
            }
            else if (std::regex_match(line, base_match, find_block))
            {
                assert(base_match.size() == 2);  // All + id
                setting_done = true;
                current_block = std::stoi(base_match[1]);
            }
            else
            {
                throw_cpu_exception_if(false, "Unrecognized expression at line " << line_number);
            }
        }

        // Check if there is no hole.
        auto i = 0;
        for (auto& variable: variables)
        {
            throw_cpu_exception_if(!variable.empty(), "Variable " << i++ << " is unassigned");
        }

        return {blocks, settings, variables};
    }

    void assemble(const Ast& ast, const SettingMap& setting_map, std::vector<std::vector<std::pair<BlockId, int>>> variables, std::ostream& output)
    {
        // Settings
        auto settings = Settings::from_ast(setting_map);
        settings.dump(output);

        auto& core_to_mem_map = setting_map.at("mem_map");
        throw_cpu_exception_if(core_to_mem_map.size() <= 0xff, "Error in mem_map. This implementation supports only 256 cores.");
        output.put(static_cast<uint8_t>(core_to_mem_map.size()));
        assert(sizeof(core_to_mem_map[0]) == 1);
        output.write(reinterpret_cast<const char*>(core_to_mem_map.data()), core_to_mem_map.size());

        // Variables
        throw_cpu_exception_if(variables.size() <= 0xff, "This implementation supports a maximum of 256 variables");
        output.put(static_cast<uint8_t>(variables.size()));

        for (auto& variable: variables)
        {
            assert(variable.size() <= 0xff);
            output.put(static_cast<char>(variable.size()));
            for (auto& j: variable)
            {
                assert(j.first <= 0xff);
                assert(j.second <= 0xff);
                output.put(static_cast<char>(j.first));
                output.put(static_cast<char>(j.second));
            }
        }


        // Instructions
        // TODO: Remove core requirement
        Core core;
        auto& factory = core.get_factory();

        for (auto& [bank_id, instructions]: ast)
        {
            throw_cpu_exception_if(bank_id <= 0xff, "This implementation supports a maximum of 256 memory banks");
            output.put(static_cast<uint8_t>(bank_id));

            throw_cpu_exception_if(instructions.size() <= 0xff, "This implementation supports only a maximum of 256 instructions per bank");
            output.put(static_cast<uint8_t>(instructions.size()));

            auto instruction_line = 1;
            for (auto& instruction_ast: instructions)
            {
                try
                {
                    auto instruction = factory.create(instruction_ast);
                    auto instruction_raw = factory.dump(*instruction);
                    output.put(instruction_raw);
                }
                catch(CpuException& exception)
                {
                    exception.add_line_infos(bank_id, instruction_line);
                    throw;
                }

                instruction_line++;
            }
        }
    }

    Cpu load_binary(std::istream& input)
    {
        // Settings
        auto settings = Settings::load(input);
        auto memory = Memory(settings);

        auto core_to_mem_map_size = char{};
        input.get(core_to_mem_map_size);
        assert(input);

        auto core_to_mem_map = std::vector<uint8_t>(core_to_mem_map_size);

        input.read(reinterpret_cast<char*>(core_to_mem_map.data()), core_to_mem_map.capacity());
        assert(input);

        // Variables
        std::vector<std::vector<std::pair<BlockId, int>>> variables;
        auto variables_number = char{};
        input.get(variables_number);
        variables.resize(variables_number);

        for (auto& variable: variables)
        {
            input.get(variables_number);
            variable.resize(variables_number);
            for (auto& affected_instructions: variable)
            {
                auto temp = char{};
                input.get(temp);
                affected_instructions.first = temp;
                input.get(temp);
                affected_instructions.second = temp;
            }
        }

        // Instructions
        while (true)
        {
            auto membank_id = char{};
            input.get(membank_id);
            if (!input)
            {
                break;
            }

            auto membank_len = char{};
            input.get(membank_len);
            assert(input);
            throw_cpu_exception_if(memory.banks_size() >= membank_len, "Using " << membank_len << " instructions out of a maximum of " << memory.banks_size() << " in membank " << membank_id);

            input.read(reinterpret_cast<char*>(memory.at(membank_id).data()), membank_len);
            assert(input);
        }

        // TODO: How to handle arguments:
        // 1. By restarting the CPU
        //   1.a. Additionally return a vector of pointers to the memory cell having the arguments
        //   1.b. Somehow store this vector inside Cpu and add a set_argument method
        // 2. At runtime
        //   2.a.
        return Cpu(settings, std::move(memory), std::move(core_to_mem_map), std::move(variables));
    }
}
