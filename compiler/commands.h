/*
This file is part of the MemeAssembly compiler.

 Copyright © 2021 Tobias Kamm

MemeAssembly is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

MemeAssembly is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with MemeAssembly. If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef MEMEASSEMBLY_COMMANDS_H
#define MEMEASSEMBLY_COMMANDS_H

#include <stdint.h>
#include <stddef.h>

#define NUMBER_OF_COMMANDS 38
#define MAX_PARAMETER_COUNT 2

#define OR_DRAW_25_OPCODE NUMBER_OF_COMMANDS - 2;
#define INVALID_COMMAND_OPCODE NUMBER_OF_COMMANDS - 1;

struct parsedCommand {
    uint8_t opcode;
    char *parameters[MAX_PARAMETER_COUNT];
    uint8_t isPointer; //0 = No Pointer, 1 = first parameter, 2 = second parameter, ...
    int lineNum;
    uint8_t translate; //Default is 1 (true). Is set to false in case this command is selected for deletion by "perfectly balanced as all things should be"
};

struct commandsArray {
    struct parsedCommand* arrayPointer;
    size_t size;
    size_t randomIndex; //A variable necessary for the "confused stonks" command
};

#define REG64 1
#define REG32 2
#define REG16 4
#define REG8 8
#define DECIMAL 16
#define CHAR 32
#define MONKE_LABEL 64
#define FUNC_NAME 128

struct command {
    char *pattern;
    uint8_t usedParameters;
    /*
     * Allowed types work as follows: Each bit is assigned to a type of variable. If it is set to one, it is allowed.
     * That way, each parameter can allow multiple variable types.
     *  Bit 0: 64 bit registers
     *  Bit 1: 32 bit registers
     *  Bit 2: 16 bit registers
     *  Bit 3: 8 bit registers
     *  Bit 4: decimal numbers
     *  Bit 5: Characters (including Escape Sequences) / ASCII-code
     *  Bit 6: Valid Monke Jump Label
     *  Bit 7: Valid function name
     */
    uint8_t allowedParamTypes[MAX_PARAMETER_COUNT];
    void (*analysisFunction)(struct commandsArray*, int);
    char *translationPattern;
};

#define commentStart "What the hell happened here?"

#define orDraw25Suffix "or draw 25"
#define orDraw25Start "or"
#define orDraw25End "draw 25"

#define pointerSuffix "do you know de wey"

#endif //MEMEASSEMBLY_COMMANDS_H
