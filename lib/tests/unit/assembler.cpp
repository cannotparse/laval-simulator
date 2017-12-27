#include <catch.hpp>
#include "assembler.h"

TEST_CASE("Assembler")
{
    auto stream = std::istringstream(R"(
.cores 1, 1, 1
.mem_number 3
.mem_size 3
.mem_map 2

1:
    NOP

2:
    ; Comment
    LCL 2
    LCH 1
    HLT
)");

    SECTION("build_ast")
    {
        auto [ast, settings] = Assembler::build_ast(stream);

        REQUIRE(ast.at(1).size() == 1);
        REQUIRE(ast.at(2).size() == 3);

        auto& instruction_1_0 = ast.at(1).at(0);
        REQUIRE(instruction_1_0.first == "NOP");
        REQUIRE(instruction_1_0.second.empty());

        auto& instruction_2_0 = ast.at(2).at(0);
        REQUIRE(instruction_2_0.first == "LCL");
        REQUIRE(instruction_2_0.second == std::vector({uint8_t{2}}));

        SECTION("assemble")
        {
            auto output = std::ostringstream();
            Assembler::assemble(ast, settings, output);
            auto binary = output.str();

            REQUIRE(binary.size() == 26);

            REQUIRE(binary.at(0) == 1);
            REQUIRE(binary.at(1) == 0);
            REQUIRE(binary.at(2) == 1);
            REQUIRE(binary.at(3) == 0);
            REQUIRE(binary.at(4) == 1);
            REQUIRE(binary.at(5) == 0);
            REQUIRE(binary.at(6) == 0);
            REQUIRE(binary.at(7) == 0);
            REQUIRE(binary.at(8) == 3);
            REQUIRE(binary.at(9) == 0);
            REQUIRE(binary.at(10) == 0);
            REQUIRE(binary.at(11) == 0);
            REQUIRE(binary.at(12) == 3);
            REQUIRE(binary.at(13) == 0);
            REQUIRE(binary.at(14) == 0);
            REQUIRE(binary.at(15) == 0);
            REQUIRE(binary.at(16) == 1);
            REQUIRE(binary.at(17) == 2);
            REQUIRE(binary.at(18) == 2);
            REQUIRE(binary.at(19) == 3);
            REQUIRE(binary.at(20) == 48);
            REQUIRE(binary.at(21) == 63);
            REQUIRE(binary.at(22) == 6);
            REQUIRE(binary.at(23) == 1);
            REQUIRE(binary.at(24) == 1);
            REQUIRE(binary.at(25) == 0);

            SECTION("load_binary")
            {
                auto binary_stream = std::istringstream(binary);
                auto cpu = Assembler::load_binary(binary_stream);
                auto result = cpu.start();
                REQUIRE(result == 18);
            }
        }
    }
}